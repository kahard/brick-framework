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

bool St7789TftDisplay::draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height, const std::uint8_t* pixels, std::size_t byte_count)
{
    const auto display_size = size();
    const auto required     = static_cast<std::size_t>(width) * height * 2;
    if (!initialized_ || pixels == nullptr || width == 0 || height == 0 || x + width > display_size.width || y + height > display_size.height || byte_count < required)
        return false;
    tft_.pushImage(x, y, width, height, const_cast<std::uint16_t*>(reinterpret_cast<const std::uint16_t*>(pixels)));
    return true;
}

}  // namespace brick::platform::esp8266
