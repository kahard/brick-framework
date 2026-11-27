#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/communication/ISerialPort.h"
#include "brick/interfaces/memory/IMemoryProvider.h"
#include "brick/platform/stm32/h7/Gpio.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

/// A DMA1 or DMA2 stream and the DMAMUX request it serves (DMA_REQUEST_...).
struct DmaStream
{
    DMA_Stream_TypeDef* stream  = nullptr;
    std::uint32_t       request = 0;
};

/// Receive errors counted since begin(); each read() counts at most one of
/// each kind.
struct Stm32UartErrors
{
    std::uint32_t framing = 0;  ///< no stop bit where expected
    std::uint32_t noise   = 0;  ///< the samples of a bit disagreed
    std::uint32_t parity  = 0;
    std::uint32_t overrun = 0;  ///< a byte arrived before the DMA took the last
};

struct Stm32UartConfig
{
    USART_TypeDef* instance = nullptr;
    AlternatePin   tx;
    AlternatePin   rx;
    std::uint32_t  baud_rate = 115200;
    DmaStream      receive_dma;
    DmaStream      transmit_dma;
    /// The ring the receive DMA fills; read() must come back before it
    /// wraps (32 KB last 65 ms at 5 Mbaud), or the oldest bytes are lost.
    std::size_t receive_bytes = 32U * 1024U;
    /// Two halves: the DMA sends one while write() fills the other.
    std::size_t transmit_bytes = 8U * 1024U;
    /// The buffers come from here (MemoryKind::dma: the DMA does not reach
    /// the DTCM).
    brick::interfaces::memory::IMemoryProvider* memory = nullptr;
};

/// 8N1 UART up to its kernel clock / 16 (6.25 Mbaud from the 100 MHz APB
/// clock; the reset kernel clock selection is assumed). Both directions run
/// on DMA without interrupts: the receiver fills a ring that read() empties;
/// write() queues bytes in the transmit half that is not being sent and
/// starts the DMA when it is idle. Either call also starts the next half, so
/// keep calling one of them while data goes out. The buffers are cached;
/// the driver cleans and invalidates them around the DMA.
class Stm32Uart final : public brick::interfaces::communication::ISerialPort
{
public:
    explicit Stm32Uart(Stm32UartConfig config) : config_(config), baud_rate_(config.baud_rate) {}

    Status      begin() override;
    std::size_t read(std::uint8_t* buffer, std::size_t capacity) override;
    std::size_t write(const std::uint8_t* data, std::size_t length) override;

    /// Sends what is queued, waits until the last bit has left, then
    /// switches both directions to the new rate. Status::timeout when the
    /// queue did not drain in time (the rate stays).
    Status        set_baud_rate(std::uint32_t baud_rate, std::uint32_t timeout_ms = 100);
    std::uint32_t baud_rate() const { return baud_rate_; }
    /// Bytes queued or being sent.
    std::size_t pending_transmit() const;
    /// Drops what has arrived and not been read.
    void                   discard_input();
    const Stm32UartErrors& errors() const { return errors_; }
    std::uint32_t line_errors() const { return errors_.framing + errors_.noise + errors_.parity + errors_.overrun; }

private:
    std::uint32_t kernel_clock_() const;
    /// BRR for the rate, 0 when the kernel clock cannot make it.
    std::uint32_t divisor_(std::uint32_t baud_rate) const;
    std::size_t   receive_head_() const;
    void          start_receive_();
    void          service_transmit_();
    void          count_line_errors_();

    Stm32UartConfig config_;
    std::uint32_t   baud_rate_;
    std::uint8_t*   receive_       = nullptr;
    std::size_t     receive_bytes_ = 0;
    std::size_t     receive_tail_  = 0;
    std::uint8_t*   transmit_      = nullptr;
    std::size_t     half_bytes_    = 0;
    std::size_t     fill_half_     = 0;  ///< the half write() fills
    std::size_t     fill_count_    = 0;
    Stm32UartErrors errors_{};
    bool            started_ = false;
};

}  // namespace brick::platform::stm32::h7
