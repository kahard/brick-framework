#include "brick/platform/stm32/h7/Stm32Uart.h"

#include <algorithm>
#include <cstring>

namespace brick::platform::stm32::h7
{

namespace
{
    // DMA1 and DMA2 each have 8 streams of 0x18 bytes from offset 0x10; the
    // DMAMUX1 channels 0-7 serve DMA1, 8-15 DMA2.
    constexpr std::uintptr_t kFirstStream    = 0x10U;
    constexpr std::uintptr_t kStreamStride   = 0x18U;
    constexpr std::uintptr_t kStreamMask     = 0xFFU;
    constexpr std::uint32_t  kStreamsPerDma  = 8U;
    constexpr std::uint32_t  kAllStreamFlags = 0x3DU;  // FEIF, DMEIF, TEIF, HTIF, TCIF
    constexpr std::uint32_t  kFlagShift[]    = { 0U, 6U, 16U, 22U };
    constexpr std::uint32_t  kStopSpins      = 100000U;
    constexpr std::size_t    kMaxTransfer    = 0xFFFFU;  // NDTR is 16 bits
    constexpr std::uint32_t  kMinDivisor     = 16U;      // 16x oversampling
    constexpr std::uint32_t  kMaxDivisor     = 0xFFFFU;
    constexpr std::uintptr_t kLine           = 32U;  // data cache line

    bool on_dma2(const DMA_Stream_TypeDef* stream)
    {
        return reinterpret_cast<std::uintptr_t>(stream) >= DMA2_BASE;
    }

    std::uint32_t stream_number(const DMA_Stream_TypeDef* stream)
    {
        return static_cast<std::uint32_t>(((reinterpret_cast<std::uintptr_t>(stream) & kStreamMask) - kFirstStream)
                                          / kStreamStride);
    }

    void clear_flags(const DMA_Stream_TypeDef* stream)
    {
        const std::uint32_t number = stream_number(stream);
        const std::uint32_t flags  = kAllStreamFlags << kFlagShift[number % 4U];
        DMA_TypeDef*        dma    = on_dma2(stream) ? DMA2 : DMA1;
        if (number < 4U)
            dma->LIFCR = flags;
        else
            dma->HIFCR = flags;
    }

    void route_request(const DmaStream& dma)
    {
        DMAMUX_Channel_TypeDef* channel =
            DMAMUX1_Channel0 + stream_number(dma.stream) + (on_dma2(dma.stream) ? kStreamsPerDma : 0U);
        channel->CCR = dma.request & DMAMUX_CxCR_DMAREQ_ID;
    }

    void enable_dma_clock(const DMA_Stream_TypeDef* stream)
    {
        if (on_dma2(stream))
            __HAL_RCC_DMA2_CLK_ENABLE();
        else
            __HAL_RCC_DMA1_CLK_ENABLE();
    }

    /// Disables the stream and waits until it has stopped.
    bool stop(DMA_Stream_TypeDef* stream)
    {
        stream->CR &= ~DMA_SxCR_EN;
        for (std::uint32_t spin = 0; (stream->CR & DMA_SxCR_EN) != 0U; ++spin)
            if (spin == kStopSpins)
                return false;
        return true;
    }

    std::uint32_t bus_address(const volatile void* pointer)
    {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
    }

    std::size_t round_up(std::size_t bytes)
    {
        return (bytes + kLine - 1U) & ~static_cast<std::size_t>(kLine - 1U);
    }
}  // namespace

Status Stm32Uart::begin()
{
    if (started_)
        return Status::ok;
    if (config_.instance == nullptr || !config_.tx.pin.connected() || !config_.rx.pin.connected()
        || config_.receive_dma.stream == nullptr || config_.transmit_dma.stream == nullptr || config_.memory == nullptr)
        return Status::invalid_argument;
    receive_bytes_              = round_up(config_.receive_bytes);
    half_bytes_                 = round_up(config_.transmit_bytes / 2U);
    const std::uint32_t divisor = divisor_(baud_rate_);
    if (divisor == 0U || receive_bytes_ == 0U || receive_bytes_ > kMaxTransfer || half_bytes_ == 0U
        || half_bytes_ > kMaxTransfer)
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.instance); !is_ok(status))
        return status;

    using brick::interfaces::memory::MemoryKind;
    receive_  = static_cast<std::uint8_t*>(config_.memory->allocate(receive_bytes_, MemoryKind::dma, kLine));
    transmit_ = static_cast<std::uint8_t*>(config_.memory->allocate(2U * half_bytes_, MemoryKind::dma, kLine));
    if (receive_ == nullptr || transmit_ == nullptr)
    {
        if (transmit_ != nullptr)
            config_.memory->release(transmit_);
        if (receive_ != nullptr)
            config_.memory->release(receive_);
        receive_  = nullptr;
        transmit_ = nullptr;
        return Status::no_memory;
    }
    enable_dma_clock(config_.receive_dma.stream);
    enable_dma_clock(config_.transmit_dma.stream);
    configure(config_.tx, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH);
    configure(config_.rx, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_SPEED_FREQ_HIGH);

    // 8N1, 16x oversampling, FIFOs on; the DMA keeps running after an error.
    USART_TypeDef* uart = config_.instance;
    uart->CR1           = 0U;
    uart->CR2           = 0U;
    uart->CR3           = USART_CR3_DMAR | USART_CR3_DMAT;
    uart->PRESC         = 0U;
    uart->BRR           = divisor;
    start_receive_();

    DMA_Stream_TypeDef* transmit = config_.transmit_dma.stream;
    if (!stop(transmit))
        return Status::busy;
    clear_flags(transmit);
    route_request(config_.transmit_dma);
    transmit->PAR = bus_address(&uart->TDR);
    transmit->FCR = 0U;                                              // direct mode
    transmit->CR  = DMA_SxCR_PL_0 | DMA_SxCR_MINC | DMA_SxCR_DIR_0;  // memory to peripheral, bytes

    uart->CR1 = USART_CR1_FIFOEN | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    started_  = true;
    return Status::ok;
}

std::size_t Stm32Uart::read(std::uint8_t* buffer, std::size_t capacity)
{
    if (!started_)
        return 0;
    service_transmit_();
    count_line_errors_();
    if (buffer == nullptr)
        return 0;
    const std::size_t head   = receive_head_();
    std::size_t       copied = 0;
    while (receive_tail_ != head && copied < capacity)
    {
        const std::size_t end   = head > receive_tail_ ? head : receive_bytes_;
        const std::size_t count = std::min(end - receive_tail_, capacity - copied);
        // The CPU never writes the ring, so dropping whole lines loses nothing.
        const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(receive_ + receive_tail_) & ~(kLine - 1U);
        const std::uintptr_t last  = reinterpret_cast<std::uintptr_t>(receive_ + receive_tail_ + count);
        SCB_InvalidateDCache_by_Addr(reinterpret_cast<void*>(first), static_cast<std::int32_t>(round_up(last - first)));
        std::memcpy(buffer + copied, receive_ + receive_tail_, count);
        receive_tail_ = (receive_tail_ + count) % receive_bytes_;
        copied += count;
    }
    return copied;
}

std::size_t Stm32Uart::write(const std::uint8_t* data, std::size_t length)
{
    if (!started_ || data == nullptr)
        return 0;
    std::size_t written = 0;
    while (written < length)
    {
        service_transmit_();
        const std::size_t room = half_bytes_ - fill_count_;
        if (room == 0U)
            break;
        const std::size_t count = std::min(room, length - written);
        std::memcpy(transmit_ + fill_half_ * half_bytes_ + fill_count_, data + written, count);
        fill_count_ += count;
        written += count;
    }
    service_transmit_();
    return written;
}

Status Stm32Uart::set_baud_rate(std::uint32_t baud_rate, std::uint32_t timeout_ms)
{
    const std::uint32_t divisor = divisor_(baud_rate);
    if (divisor == 0U)
        return Status::invalid_argument;
    if (!started_)
    {
        baud_rate_ = baud_rate;
        return Status::ok;
    }
    // TC: the transmit FIFO is empty and the last stop bit has left.
    const std::uint32_t from = HAL_GetTick();
    while (pending_transmit() != 0U || (config_.instance->ISR & USART_ISR_TC) == 0U)
    {
        service_transmit_();
        if (HAL_GetTick() - from > timeout_ms)
            return Status::timeout;
    }
    // BRR is writable only while the UART is disabled; the DMA streams stay.
    config_.instance->CR1 &= ~USART_CR1_UE;
    config_.instance->BRR = divisor;
    config_.instance->CR1 |= USART_CR1_UE;
    baud_rate_ = baud_rate;
    return Status::ok;
}

std::size_t Stm32Uart::pending_transmit() const
{
    if (!started_)
        return 0;
    const DMA_Stream_TypeDef* stream  = config_.transmit_dma.stream;
    const std::size_t         sending = (stream->CR & DMA_SxCR_EN) != 0U ? stream->NDTR : 0U;
    return fill_count_ + sending;
}

void Stm32Uart::discard_input()
{
    if (started_)
        receive_tail_ = receive_head_();
}

std::uint32_t Stm32Uart::kernel_clock_() const
{
    // Reset selection: USART1 and USART6 run on PCLK2, the others on PCLK1.
    if (config_.instance == USART1 || config_.instance == USART6)
        return HAL_RCC_GetPCLK2Freq();
    return HAL_RCC_GetPCLK1Freq();
}

std::uint32_t Stm32Uart::divisor_(std::uint32_t baud_rate) const
{
    if (baud_rate == 0U)
        return 0U;
    const std::uint32_t divisor = (kernel_clock_() + baud_rate / 2U) / baud_rate;
    return divisor >= kMinDivisor && divisor <= kMaxDivisor ? divisor : 0U;
}

std::size_t Stm32Uart::receive_head_() const
{
    // NDTR counts down from the ring size and reloads at zero.
    const std::size_t remaining = std::min<std::size_t>(config_.receive_dma.stream->NDTR, receive_bytes_);
    const std::size_t head      = receive_bytes_ - remaining;
    return head == receive_bytes_ ? 0U : head;
}

void Stm32Uart::start_receive_()
{
    DMA_Stream_TypeDef* stream = config_.receive_dma.stream;
    (void)stop(stream);
    clear_flags(stream);
    route_request(config_.receive_dma);
    // No dirty line of the ring may be written back over what the DMA stores.
    SCB_InvalidateDCache_by_Addr(receive_, static_cast<std::int32_t>(receive_bytes_));
    stream->PAR  = bus_address(&config_.instance->RDR);
    stream->M0AR = bus_address(receive_);
    stream->NDTR = static_cast<std::uint32_t>(receive_bytes_);
    stream->FCR  = 0U;                                             // direct mode
    stream->CR   = DMA_SxCR_PL_1 | DMA_SxCR_MINC | DMA_SxCR_CIRC;  // peripheral to memory, bytes
    stream->CR |= DMA_SxCR_EN;
    receive_tail_ = 0;
}

void Stm32Uart::service_transmit_()
{
    DMA_Stream_TypeDef* stream = config_.transmit_dma.stream;
    if (fill_count_ == 0U || (stream->CR & DMA_SxCR_EN) != 0U)
        return;
    std::uint8_t* half = transmit_ + fill_half_ * half_bytes_;
    SCB_CleanDCache_by_Addr(reinterpret_cast<std::uint32_t*>(half), static_cast<std::int32_t>(round_up(fill_count_)));
    clear_flags(stream);
    stream->M0AR = bus_address(half);
    stream->NDTR = static_cast<std::uint32_t>(fill_count_);
    stream->CR |= DMA_SxCR_EN;
    fill_half_ ^= 1U;
    fill_count_ = 0;
}

void Stm32Uart::count_line_errors_()
{
    const std::uint32_t flags = config_.instance->ISR;
    std::uint32_t       clear = 0;
    if ((flags & USART_ISR_FE) != 0U)
    {
        ++errors_.framing;
        clear |= USART_ICR_FECF;
    }
    if ((flags & USART_ISR_NE) != 0U)
    {
        ++errors_.noise;
        clear |= USART_ICR_NECF;
    }
    if ((flags & USART_ISR_PE) != 0U)
    {
        ++errors_.parity;
        clear |= USART_ICR_PECF;
    }
    if ((flags & USART_ISR_ORE) != 0U)
    {
        ++errors_.overrun;
        clear |= USART_ICR_ORECF;
    }
    if (clear != 0U)
        config_.instance->ICR = clear;
}

}  // namespace brick::platform::stm32::h7
