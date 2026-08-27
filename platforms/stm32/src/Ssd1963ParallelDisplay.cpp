#include "brick/platform/stm32/f1/Ssd1963ParallelDisplay.h"

namespace brick::platform::stm32::f1
{

Ssd1963ParallelDisplay::Ssd1963ParallelDisplay(Ssd1963ParallelDisplayConfig config) : config_(config)
{
}

bool Ssd1963ParallelDisplay::begin()
{
    if (started_ || config_.data_port == nullptr || config_.data_mask == 0)
        return started_;
    configure_output_(config_.data_port, config_.data_mask);
    configure_output_(config_.rs_port, config_.rs_pin);
    configure_output_(config_.wr_port, config_.wr_pin);
    configure_output_(config_.rd_port, config_.rd_pin);
    configure_output_(config_.cs_port, config_.cs_pin);
    configure_output_(config_.reset_port, config_.reset_pin);
    configure_output_(config_.display_enable_port, config_.display_enable_pin);
    write_bus_(0);
    write_pin_(config_.rd_port, config_.rd_pin, true);
    write_pin_(config_.rs_port, config_.rs_pin, true);
    write_pin_(config_.wr_port, config_.wr_pin, true);
    write_pin_(config_.cs_port, config_.cs_pin, true);
    write_pin_(config_.reset_port, config_.reset_pin, true);
    // ST-280's legacy startup switches the panel supply gate before it sends
    // the SSD1963 reset sequence. Keep these delays verbatim: lighting only
    // the backlight without this transition leaves the controller blank.
    write_pin_(config_.display_enable_port, config_.display_enable_pin, false);
    HAL_Delay(200);
    write_pin_(config_.display_enable_port, config_.display_enable_pin, true);
    HAL_Delay(150);
    write_pin_(config_.reset_port, config_.reset_pin, false);
    HAL_Delay(100);
    write_pin_(config_.reset_port, config_.reset_pin, true);
    HAL_Delay(100);
    started_ = initialize_controller_();
    return started_;
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
    return { brick::interfaces::display::DisplayPanelType::host, size(), pixel_format(), 0, 1, 0, false, false, false, true, false, 1, brick::interfaces::display::RenderMode::partial };
}

bool Ssd1963ParallelDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    if (!started_ || rotation != brick::interfaces::display::Rotation::rotate_0)
        return false;
    command_(0x36);
    data_(0x00);
    return true;
}

bool Ssd1963ParallelDisplay::draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer)
{
    constexpr std::size_t bytes_per_pixel = 2;
    if (!started_ || area.empty() || area.x < 0 || area.y < 0 || area.x + area.width > config_.width || area.y + area.height > config_.height || !buffer.valid() || buffer.width != static_cast<std::uint32_t>(area.width)
        || buffer.height != static_cast<std::uint32_t>(area.height) || buffer.format != pixel_format() || buffer.stride_bytes < static_cast<std::size_t>(area.width) * bytes_per_pixel || !set_window_(area.x, area.y, area.width, area.height))
        return false;

    command_(0x2C);
    for (std::uint32_t row = 0; row < buffer.height; ++row)
    {
        const auto* pixels = reinterpret_cast<const std::uint16_t*>(buffer.data + static_cast<std::size_t>(row) * buffer.stride_bytes);
        for (std::uint32_t column = 0; column < buffer.width; ++column)
        {
            const auto value = pixels[column];
            data_(static_cast<std::uint8_t>((value >> 8) & 0xF8));
            data_(static_cast<std::uint8_t>((value >> 3) & 0xFC));
            data_(static_cast<std::uint8_t>((value << 3) & 0xF8));
        }
    }
    return true;
}

void Ssd1963ParallelDisplay::configure_output_(GPIO_TypeDef* port, std::uint16_t pins) const
{
    GPIO_InitTypeDef init{};
    init.Pin   = pins;
    init.Mode  = GPIO_MODE_OUTPUT_PP;
    init.Pull  = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &init);
}

void Ssd1963ParallelDisplay::write_pin_(GPIO_TypeDef* port, std::uint16_t pin, bool high) const
{
    port->BSRR = high ? pin : static_cast<std::uint32_t>(pin) << 16;
}
void Ssd1963ParallelDisplay::write_bus_(std::uint8_t value) const
{
    config_.data_port->BSRR = (static_cast<std::uint32_t>(config_.data_mask & ~value) << 16) | (value & config_.data_mask);
}
void Ssd1963ParallelDisplay::pulse_write_() const
{
    write_pin_(config_.wr_port, config_.wr_pin, false);
    write_pin_(config_.wr_port, config_.wr_pin, true);
}
void Ssd1963ParallelDisplay::command_(std::uint8_t value) const
{
    write_pin_(config_.cs_port, config_.cs_pin, false);
    write_pin_(config_.rs_port, config_.rs_pin, false);
    write_bus_(value);
    pulse_write_();
    write_pin_(config_.cs_port, config_.cs_pin, true);
}
void Ssd1963ParallelDisplay::data_(std::uint8_t value) const
{
    write_pin_(config_.cs_port, config_.cs_pin, false);
    write_pin_(config_.rs_port, config_.rs_pin, true);
    write_bus_(value);
    pulse_write_();
    write_pin_(config_.cs_port, config_.cs_pin, true);
}

bool Ssd1963ParallelDisplay::initialize_controller_()
{
    command_(0x01);
    command_(0x01);
    command_(0x01);
    HAL_Delay(100);
    command_(0xE0);
    data_(0x01);
    HAL_Delay(1);
    command_(0xE0);
    data_(0x03);
    HAL_Delay(1);
    command_(0xB0);
    data_(0x00);
    data_(0x00);
    data_((config_.width - 1) >> 8);
    data_(config_.width - 1);
    data_((config_.height - 1) >> 8);
    data_(config_.height - 1);
    data_(0x00);
    command_(0xF0);
    data_(0x00);
    command_(0x3A);
    data_(0x60);
    command_(0xE6);
    data_(config_.pll_m);
    data_(config_.pll_n);
    data_(config_.pll_k);
    command_(0xB4);
    data_(0x02);
    data_(0x0D);
    data_(0x00);
    data_(0x2B);
    data_(0x28);
    data_(0x00);
    data_(0x00);
    data_(0x00);
    command_(0xB6);
    data_(0x01);
    data_(0x1D);
    data_(0x00);
    data_(0x0C);
    data_(0x09);
    data_(0x00);
    data_(0x00);
    return set_window_(0, 0, config_.width, config_.height) && (command_(0x29), true);
}

bool Ssd1963ParallelDisplay::set_window_(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height) const
{
    if (width == 0 || height == 0)
        return false;
    const auto x2 = static_cast<std::uint16_t>(x + width - 1);
    const auto y2 = static_cast<std::uint16_t>(y + height - 1);
    command_(0x2A);
    data_(x >> 8);
    data_(x);
    data_(x2 >> 8);
    data_(x2);
    command_(0x2B);
    data_(y >> 8);
    data_(y);
    data_(y2 >> 8);
    data_(y2);
    return true;
}

}  // namespace brick::platform::stm32::f1
