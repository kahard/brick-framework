#include "brick/platform/esp8266/St7789TftDisplay.h"

namespace brick::platform::esp8266
{

St7789TftDisplay::St7789TftDisplay(St7789TftDisplayConfig config) : config_(config)
{
}

bool St7789TftDisplay::begin()
{
    if (initialized_)
        return true;
    if (config_.width == 0 || config_.height == 0 || config_.rotation > 3)
        return false;
    if (config_.enable_backlight)
    {
        pinMode(config_.backlight_gpio, OUTPUT);
        digitalWrite(config_.backlight_gpio, config_.backlight_on_level);
    }
    tft_.init();
    tft_.setSwapBytes(config_.swap_bytes);
    tft_.setRotation(config_.rotation);
    initialized_ = true;
    return true;
}

brick::interfaces::display::DisplaySize St7789TftDisplay::size() const
{
    return config_.rotation % 2 == 0 ? brick::interfaces::display::DisplaySize{ config_.width, config_.height } : brick::interfaces::display::DisplaySize{ config_.height, config_.width };
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
        static_cast<std::size_t>(size().width) * size().height * brick::interfaces::display::pixel_format_bytes(pixel_format()),
        4,
        0,
        false,
        false,
        false,
        true,
        false,
        1,
        brick::interfaces::display::RenderMode::partial,
    };
}

bool St7789TftDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    const auto value = static_cast<std::uint8_t>(rotation);
    if (value > 3)
        return false;
    config_.rotation = value;
    if (initialized_)
        tft_.setRotation(config_.rotation);
    return true;
}

bool St7789TftDisplay::draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer)
{
    const auto display_size = size();
    if (!initialized_ || area.empty() || area.x < 0 || area.y < 0 || area.x + area.width > display_size.width || area.y + area.height > display_size.height ||
        !buffer.valid() || buffer.width != static_cast<std::uint32_t>(area.width) || buffer.height != static_cast<std::uint32_t>(area.height) ||
        buffer.format != pixel_format() || buffer.stride_bytes != static_cast<std::size_t>(area.width) * 2)
        return false;
    tft_.pushImage(static_cast<std::uint16_t>(area.x), static_cast<std::uint16_t>(area.y), static_cast<std::uint16_t>(area.width),
                   static_cast<std::uint16_t>(area.height), const_cast<std::uint16_t*>(reinterpret_cast<const std::uint16_t*>(buffer.data)));
    return true;
}

}  // namespace brick::platform::esp8266
