#include "brick/platform/stm32/f1/Ssd1963ParallelDisplay.h"

#include <initializer_list>

namespace brick::platform::stm32::f1
{

namespace
{

    constexpr std::uint16_t kDataPins = 0x00FF;

    /// Port value that puts value on D0..D7 in one BSRR write.
    constexpr std::uint32_t bus_word(std::uint8_t value)
    {
        return (static_cast<std::uint32_t>(kDataPins & ~value) << 16) | value;
    }

    /// The registers of one data burst, kept in locals so the byte loop
    /// does not reload the configuration.
    struct BurstWriter
    {
        GPIO_TypeDef* data_port;
        GPIO_TypeDef* wr_port;
        std::uint32_t wr_low;
        std::uint32_t wr_high;

        void strobe() const
        {
            wr_port->BSRR = wr_low;
            wr_port->BSRR = wr_high;
        }
        void write(std::uint32_t word) const
        {
            data_port->BSRR = word;
            strobe();
        }
    };
    // Delays of the tested power-up sequence: on tested panels, lighting the
    // backlight without the supply gate transition leaves the controller blank.
    constexpr std::uint32_t kSupplyOffMs     = 200;
    constexpr std::uint32_t kSupplySettleMs  = 150;
    constexpr std::uint32_t kResetPulseMs    = 100;
    constexpr std::uint32_t kResetRecoveryMs = 100;
    constexpr std::uint32_t kSoftResetMs     = 100;
    constexpr std::uint32_t kPllLockMs       = 1;

    constexpr std::uint8_t kSoftReset          = 0x01;
    constexpr std::uint8_t kDisplayOn          = 0x29;
    constexpr std::uint8_t kSetColumnAddress   = 0x2A;
    constexpr std::uint8_t kSetPageAddress     = 0x2B;
    constexpr std::uint8_t kWriteMemoryStart   = 0x2C;
    constexpr std::uint8_t kSetAddressMode     = 0x36;
    constexpr std::uint8_t kSetPixelFormat     = 0x3A;
    constexpr std::uint8_t kSetLcdMode         = 0xB0;
    constexpr std::uint8_t kSetHorizontalTimes = 0xB4;
    constexpr std::uint8_t kSetVerticalTimes   = 0xB6;
    constexpr std::uint8_t kSetPll             = 0xE0;
    constexpr std::uint8_t kSetPixelClock      = 0xE6;
    constexpr std::uint8_t kSetPixelInterface  = 0xF0;

}  // namespace

Status Ssd1963ParallelDisplay::begin()
{
    if (started_)
        return Status::ok;
    if (config_.width == 0 || config_.height == 0 || config_.timing.horizontal_total == 0
        || config_.timing.vertical_total == 0 || config_.timing.pixel_clock_ratio == 0 || config_.data_port == nullptr
        || !config_.rs.connected() || !config_.wr.connected() || !config_.cs.connected())
        return Status::invalid_argument;
    configure({ config_.data_port, kDataPins }, GPIO_MODE_OUTPUT_PP);
    for (const GpioPin& pin : { config_.rs, config_.wr, config_.cs, config_.rd, config_.reset, config_.display_enable })
        configure(pin, GPIO_MODE_OUTPUT_PP);
    write_bus_(0);
    for (const GpioPin& pin : { config_.rd, config_.rs, config_.wr, config_.cs, config_.reset })
        write(pin, true);
    power_up_panel_();
    const Status status = initialize_controller_();
    started_            = is_ok(status);
    return status;
}

brick::interfaces::display::DisplaySize Ssd1963ParallelDisplay::size() const
{
    return { config_.width, config_.height };
}

brick::interfaces::display::PixelFormat Ssd1963ParallelDisplay::pixel_format() const
{
    return brick::interfaces::display::PixelFormat::rgb565;
}

brick::interfaces::display::DisplayCapabilities Ssd1963ParallelDisplay::capabilities() const
{
    return { brick::interfaces::display::DisplayPanelType::host,
             size(),
             pixel_format(),
             0,
             1,
             0,
             false,
             false,
             false,
             true,
             false,
             1,
             brick::interfaces::display::RenderMode::partial };
}

Status Ssd1963ParallelDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    if (!started_)
        return Status::not_ready;
    if (rotation != brick::interfaces::display::Rotation::rotate_0)
        return Status::not_supported;
    command_(kSetAddressMode);
    data_(0x00);
    return Status::ok;
}

Status Ssd1963ParallelDisplay::draw_buffer(brick::interfaces::display::DisplayRect        area,
                                           const brick::interfaces::display::PixelBuffer& buffer)
{
    constexpr std::size_t bytes_per_pixel = 2;
    if (!started_)
        return Status::not_ready;
    if (!inside_(area) || !buffer.valid() || buffer.width != static_cast<std::uint32_t>(area.width)
        || buffer.height != static_cast<std::uint32_t>(area.height) || buffer.format != pixel_format()
        || buffer.stride_bytes < static_cast<std::size_t>(area.width) * bytes_per_pixel)
        return Status::invalid_argument;
    if (const Status status = begin_burst_(area); !is_ok(status))
        return status;

    // The controller takes 8-bit red, green and blue for every pixel.
    const BurstWriter writer{ config_.data_port, config_.wr.port, static_cast<std::uint32_t>(config_.wr.pin) << 16,
                              config_.wr.pin };
    for (std::uint32_t row = 0; row < buffer.height; ++row)
    {
        const auto* pixels =
            reinterpret_cast<const std::uint16_t*>(buffer.data + static_cast<std::size_t>(row) * buffer.stride_bytes);
        for (std::uint32_t column = 0; column < buffer.width; ++column)
        {
            const auto value = pixels[column];
            writer.write(bus_word(static_cast<std::uint8_t>((value >> 8) & 0xF8)));
            writer.write(bus_word(static_cast<std::uint8_t>((value >> 3) & 0xFC)));
            writer.write(bus_word(static_cast<std::uint8_t>((value << 3) & 0xF8)));
        }
    }
    end_burst_();
    return Status::ok;
}

Status Ssd1963ParallelDisplay::fill_rect(brick::interfaces::display::DisplayRect area, std::uint16_t color)
{
    if (!started_)
        return Status::not_ready;
    if (!inside_(area))
        return Status::invalid_argument;
    if (const Status status = begin_burst_(area); !is_ok(status))
        return status;
    const BurstWriter   writer{ config_.data_port, config_.wr.port, static_cast<std::uint32_t>(config_.wr.pin) << 16,
                              config_.wr.pin };
    const auto          red    = static_cast<std::uint8_t>((color >> 8) & 0xF8);
    const auto          green  = static_cast<std::uint8_t>((color >> 3) & 0xFC);
    const auto          blue   = static_cast<std::uint8_t>((color << 3) & 0xF8);
    const std::uint32_t pixels = static_cast<std::uint32_t>(area.width) * static_cast<std::uint32_t>(area.height);
    if (red == green && green == blue)
    {
        // The data lines stay as they are; only WR moves.
        config_.data_port->BSRR = bus_word(red);
        for (std::uint32_t pixel = 0; pixel < pixels; ++pixel)
        {
            writer.strobe();
            writer.strobe();
            writer.strobe();
        }
    }
    else
    {
        const std::uint32_t words[] = { bus_word(red), bus_word(green), bus_word(blue) };
        for (std::uint32_t pixel = 0; pixel < pixels; ++pixel)
        {
            writer.write(words[0]);
            writer.write(words[1]);
            writer.write(words[2]);
        }
    }
    end_burst_();
    return Status::ok;
}

bool Ssd1963ParallelDisplay::inside_(brick::interfaces::display::DisplayRect area) const
{
    return !area.empty() && area.x >= 0 && area.y >= 0 && area.x + area.width <= config_.width
           && area.y + area.height <= config_.height;
}

Status Ssd1963ParallelDisplay::begin_burst_(brick::interfaces::display::DisplayRect area) const
{
    if (const Status status =
            set_window_(static_cast<std::uint16_t>(area.x), static_cast<std::uint16_t>(area.y),
                        static_cast<std::uint16_t>(area.width), static_cast<std::uint16_t>(area.height));
        !is_ok(status))
        return status;
    command_(kWriteMemoryStart);
    write(config_.cs, false);
    write(config_.rs, true);
    return Status::ok;
}

void Ssd1963ParallelDisplay::end_burst_() const
{
    write(config_.cs, true);
}

void Ssd1963ParallelDisplay::power_up_panel_() const
{
    // Cycle the optional panel supply gate, then reset the controller.
    write(config_.display_enable, false);
    HAL_Delay(kSupplyOffMs);
    write(config_.display_enable, true);
    HAL_Delay(kSupplySettleMs);
    write(config_.reset, false);
    HAL_Delay(kResetPulseMs);
    write(config_.reset, true);
    HAL_Delay(kResetRecoveryMs);
}

void Ssd1963ParallelDisplay::write_bus_(std::uint8_t value) const
{
    config_.data_port->BSRR = (static_cast<std::uint32_t>(kDataPins & ~value) << 16) | (value & kDataPins);
}

void Ssd1963ParallelDisplay::pulse_write_() const
{
    write(config_.wr, false);
    write(config_.wr, true);
}

void Ssd1963ParallelDisplay::command_(std::uint8_t value) const
{
    write(config_.cs, false);
    write(config_.rs, false);
    write_bus_(value);
    pulse_write_();
    write(config_.cs, true);
}

void Ssd1963ParallelDisplay::data_(std::uint8_t value) const
{
    write(config_.cs, false);
    write(config_.rs, true);
    write_bus_(value);
    pulse_write_();
    write(config_.cs, true);
}

void Ssd1963ParallelDisplay::data16_(std::uint16_t value) const
{
    data_(static_cast<std::uint8_t>(value >> 8));
    data_(static_cast<std::uint8_t>(value));
}

Status Ssd1963ParallelDisplay::initialize_controller_()
{
    const Ssd1963PanelTiming& timing = config_.timing;
    command_(kSoftReset);
    command_(kSoftReset);
    command_(kSoftReset);
    HAL_Delay(kSoftResetMs);
    command_(kSetPll);
    data_(0x01);  // enable
    HAL_Delay(kPllLockMs);
    command_(kSetPll);
    data_(0x03);  // lock
    HAL_Delay(kPllLockMs);
    command_(kSetLcdMode);
    data_(0x00);
    data_(0x00);
    data16_(config_.width - 1);
    data16_(config_.height - 1);
    data_(0x00);
    command_(kSetPixelInterface);
    data_(0x00);  // 8-bit bus
    command_(kSetPixelFormat);
    data_(0x60);
    command_(kSetPixelClock);
    data_(static_cast<std::uint8_t>(timing.pixel_clock_ratio >> 16));
    data16_(static_cast<std::uint16_t>(timing.pixel_clock_ratio));
    command_(kSetHorizontalTimes);
    data16_(timing.horizontal_total);
    data16_(timing.horizontal_sync_start);
    data_(timing.horizontal_sync_width);
    data16_(0);   // LPS
    data_(0x00);  // LPSPP
    command_(kSetVerticalTimes);
    data16_(timing.vertical_total);
    data16_(timing.vertical_sync_start);
    data_(timing.vertical_sync_width);
    data16_(0);  // FPS
    if (const Status status = set_window_(0, 0, config_.width, config_.height); !is_ok(status))
        return status;
    command_(kDisplayOn);
    return Status::ok;
}

Status Ssd1963ParallelDisplay::set_window_(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                                           std::uint16_t height) const
{
    if (width == 0 || height == 0)
        return Status::invalid_argument;
    command_(kSetColumnAddress);
    data16_(x);
    data16_(static_cast<std::uint16_t>(x + width - 1));
    command_(kSetPageAddress);
    data16_(y);
    data16_(static_cast<std::uint16_t>(y + height - 1));
    return Status::ok;
}

}  // namespace brick::platform::stm32::f1
