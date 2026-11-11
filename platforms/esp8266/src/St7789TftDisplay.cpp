#include "brick/platform/esp8266/St7789TftDisplay.h"

#include <atomic>

namespace brick::platform::esp8266
{

namespace
{
    // The HSPI has no DMA: the CPU feeds its 64-byte buffer W0..W15, one
    // command at a time. TFT_eSPI's pushImage() loads 32 bytes, byte by byte
    // when it swaps them, and waits for them to go out before the next ones;
    // the line idles meanwhile. Everything here runs from IRAM: streaming an
    // image through the flash cache would evict it.
    constexpr std::uint32_t  kHalfPixels   = 16;  // W0..W7 or W8..W15
    constexpr std::uint32_t  kBufferPixels = 32;  // W0..W15
    constexpr std::uintptr_t kDramStart    = 0x3FFE8000U;
    constexpr std::uintptr_t kDramEnd      = 0x40000000U;

    [[gnu::always_inline]] inline std::uint32_t swap_halves(std::uint32_t word)
    {
        return ((word >> 8) & 0x00FF00FFU) | ((word << 8) & 0xFF00FF00U);
    }

    /// Copies up to count pixels into the SPI buffer at out (swapped when the
    /// panel wants the high byte first) and returns the words used.
    [[gnu::always_inline]] inline std::uint32_t load(volatile std::uint32_t* out, const std::uint32_t* words,
                                                     std::uint32_t count, bool swap)
    {
        const std::uint32_t used = (count + 1) / 2;
        if (swap)
            for (std::uint32_t index = 0; index < used; ++index)
                out[index] = swap_halves(words[index]);
        else
            for (std::uint32_t index = 0; index < used; ++index)
                out[index] = words[index];
        return used;
    }

    /// The SPI user register for sending only: in full duplex the received
    /// bits would land in the buffer being filled.
    [[gnu::always_inline]] inline std::uint32_t send_only(std::uint32_t user)
    {
        return (user & ~(SPIUDUPLEX | SPIUMISO | SPIUMISOH | SPIUMOSIH)) | SPIUMOSI;
    }

    /// Synchronous: fills one half of the buffer while the other one goes out
    /// (MOSI from W8..W15 when SPIUMOSIH is set).
    [[gnu::noinline]] IRAM_ATTR void push_pixels(const std::uint32_t* words, std::uint32_t pixels, bool swap)
    {
        const std::uint32_t user                = SPI1U;
        SPI1U                                   = send_only(user);
        volatile std::uint32_t* const halves[2] = { &SPI1W0, &SPI1W8 };
        std::uint32_t                 half      = 0;
        while (pixels > 0)
        {
            const std::uint32_t count = pixels < kHalfPixels ? pixels : kHalfPixels;
            words += load(halves[half], words, count, swap);
            pixels -= count;
            while (SPI1CMD & SPIBUSY)
            {
            }
            SPI1U1 = (count * 16 - 1) << SPILMOSI;
            SPI1U  = half == 0 ? SPI1U & ~SPIUMOSIH : SPI1U | SPIUMOSIH;
            SPI1CMD |= SPIBUSY;
            half ^= 1;
        }
        while (SPI1CMD & SPIBUSY)
        {
        }
        SPI1U = user;
    }

    /// The transfer the HSPI interrupt feeds (there is one HSPI).
    struct Transfer
    {
        const std::uint32_t* words  = nullptr;
        std::uint32_t        pixels = 0;
        std::uint32_t        user   = 0;  ///< SPI1U to restore
        bool                 swap   = false;
        std::atomic<bool>    active{ false };
    };
    Transfer transfer;

    /// Sends the next (up to) 64 bytes; the SPI is idle.
    IRAM_ATTR void send_next()
    {
        const std::uint32_t count = transfer.pixels < kBufferPixels ? transfer.pixels : kBufferPixels;
        transfer.words += load(&SPI1W0, transfer.words, count, transfer.swap);
        transfer.pixels -= count;
        SPI1U1 = (count * 16 - 1) << SPILMOSI;
        SPI1CMD |= SPIBUSY;
    }

    /// SPI0 and HSPI share the interrupt; only the HSPI one is enabled.
    IRAM_ATTR void spi_interrupt(void*, void*)
    {
        const std::uint32_t sources = SPIIR;
        if ((sources & (1U << SPII0)) != 0)
            SPI0S &= ~0x3FFU;  // as the core's SPISlave: clear and mask SPI0
        if ((sources & (1U << SPII1)) == 0)
            return;
        SPI1S &= ~SPISTRIS;
        if (transfer.pixels > 0)
        {
            send_next();
            return;
        }
        SPI1S &= ~SPISTRIE;
        transfer.active.store(false, std::memory_order_release);
    }
}  // namespace

St7789TftDisplay::St7789TftDisplay(St7789TftDisplayConfig config) : config_(config)
{
}

Status St7789TftDisplay::begin()
{
    if (initialized_)
        return Status::ok;
    if (config_.width == 0 || config_.height == 0 || config_.rotation > 3)
        return Status::invalid_argument;
    if (config_.enable_backlight)
    {
        pinMode(config_.backlight_gpio, OUTPUT);
        digitalWrite(config_.backlight_gpio, config_.backlight_on_level);
    }
    tft_.init();
    tft_.setSwapBytes(config_.swap_bytes);
    tft_.setRotation(config_.rotation);
    SPI1S &= ~(SPISTRIE | SPISTRIS);
    ETS_SPI_INTR_ATTACH(spi_interrupt, nullptr);
    ETS_SPI_INTR_ENABLE();
    initialized_ = true;
    return Status::ok;
}

brick::interfaces::display::DisplaySize St7789TftDisplay::size() const
{
    return config_.rotation % 2 == 0 ? brick::interfaces::display::DisplaySize{ config_.width, config_.height } :
                                       brick::interfaces::display::DisplaySize{ config_.height, config_.width };
}

brick::interfaces::display::PixelFormat St7789TftDisplay::pixel_format() const
{
    return brick::interfaces::display::PixelFormat::rgb565;
}

brick::interfaces::display::DisplayCapabilities St7789TftDisplay::capabilities() const
{
    return {
        brick::interfaces::display::DisplayPanelType::spi,
        size(),
        pixel_format(),
        static_cast<std::size_t>(size().width) * size().height
            * brick::interfaces::display::pixel_format_bytes(pixel_format()),
        4,
        0,
        false,
        false,
        false,
        true,
        true,
        1,
        brick::interfaces::display::RenderMode::partial,
    };
}

Status St7789TftDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    const auto value = static_cast<std::uint8_t>(rotation);
    if (value > 3)
        return Status::invalid_argument;
    if (const Status status = finish_transfer_(); !is_ok(status))
        return status;
    config_.rotation = value;
    if (initialized_)
        tft_.setRotation(config_.rotation);
    return Status::ok;
}

Status St7789TftDisplay::draw_buffer(brick::interfaces::display::DisplayRect        area,
                                     const brick::interfaces::display::PixelBuffer& buffer)
{
    if (const Status status = check_(area, buffer); !is_ok(status))
        return status;
    if (const Status status = finish_transfer_(); !is_ok(status))
        return status;
    if (reinterpret_cast<std::uintptr_t>(buffer.data) % sizeof(std::uint32_t) != 0)
    {
        tft_.pushImage(static_cast<std::uint16_t>(area.x), static_cast<std::uint16_t>(area.y),
                       static_cast<std::uint16_t>(area.width), static_cast<std::uint16_t>(area.height),
                       const_cast<std::uint16_t*>(reinterpret_cast<const std::uint16_t*>(buffer.data)));
        return Status::ok;
    }
    tft_.startWrite();
    tft_.setWindow(area.x, area.y, area.x + area.width - 1, area.y + area.height - 1);
    push_pixels(reinterpret_cast<const std::uint32_t*>(buffer.data),
                static_cast<std::uint32_t>(area.width) * static_cast<std::uint32_t>(area.height), config_.swap_bytes);
    tft_.endWrite();
    return Status::ok;
}

Status St7789TftDisplay::submit_buffer(brick::interfaces::display::DisplayRect        area,
                                       const brick::interfaces::display::PixelBuffer& buffer)
{
    // The interrupt reads the pixels while the caller works, also while the
    // flash cache is off (a flash write): only a word-aligned buffer in DRAM.
    const auto address = reinterpret_cast<std::uintptr_t>(buffer.data);
    if (address % sizeof(std::uint32_t) != 0 || address < kDramStart || address >= kDramEnd)
        return draw_buffer(area, buffer);
    if (const Status status = check_(area, buffer); !is_ok(status))
        return status;
    if (const Status status = finish_transfer_(); !is_ok(status))
        return status;
    tft_.startWrite();
    tft_.setWindow(area.x, area.y, area.x + area.width - 1, area.y + area.height - 1);
    transfer.words  = reinterpret_cast<const std::uint32_t*>(buffer.data);
    transfer.pixels = static_cast<std::uint32_t>(area.width) * static_cast<std::uint32_t>(area.height);
    transfer.swap   = config_.swap_bytes;
    transfer.user   = SPI1U;
    transfer.active.store(true, std::memory_order_relaxed);
    SPI1U = send_only(transfer.user);
    SPI1S = (SPI1S & ~SPISTRIS) | SPISTRIE;
    send_next();
    transferring_ = true;
    return Status::ok;
}

Status St7789TftDisplay::wait_for_transfer_complete(std::uint32_t timeout_ms)
{
    if (!transferring_)
        return Status::ok;
    const std::uint32_t started = millis();
    while (transfer.active.load(std::memory_order_acquire))
    {
        if (millis() - started >= timeout_ms)
        {
            // Give up: no more interrupts, let the last command end.
            SPI1S &= ~(SPISTRIE | SPISTRIS);
            transfer.active.store(false, std::memory_order_relaxed);
            end_transfer_();
            return Status::timeout;
        }
    }
    end_transfer_();
    return Status::ok;
}

Status St7789TftDisplay::fill_rect(brick::interfaces::display::DisplayRect area, std::uint16_t color)
{
    if (!initialized_)
        return Status::not_ready;
    if (!inside_(area))
        return Status::invalid_argument;
    if (const Status status = finish_transfer_(); !is_ok(status))
        return status;
    tft_.fillRect(area.x, area.y, area.width, area.height, color);
    return Status::ok;
}

Status St7789TftDisplay::check_(brick::interfaces::display::DisplayRect        area,
                                const brick::interfaces::display::PixelBuffer& buffer) const
{
    if (!initialized_)
        return Status::not_ready;
    if (!inside_(area) || !buffer.valid() || buffer.width != static_cast<std::uint32_t>(area.width)
        || buffer.height != static_cast<std::uint32_t>(area.height) || buffer.format != pixel_format()
        || buffer.stride_bytes != static_cast<std::size_t>(area.width) * 2)
        return Status::invalid_argument;
    return Status::ok;
}

Status St7789TftDisplay::finish_transfer_()
{
    return wait_for_transfer_complete(kTransferTimeoutMs);
}

void St7789TftDisplay::end_transfer_()
{
    while (SPI1CMD & SPIBUSY)
    {
    }
    SPI1U         = transfer.user;
    transferring_ = false;
    tft_.endWrite();
}

bool St7789TftDisplay::inside_(brick::interfaces::display::DisplayRect area) const
{
    const auto display_size = size();
    return !area.empty() && area.x >= 0 && area.y >= 0 && area.x + area.width <= display_size.width
           && area.y + area.height <= display_size.height;
}

}  // namespace brick::platform::esp8266
