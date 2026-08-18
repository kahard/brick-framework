#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"

namespace brick::platform::esp8266
{

struct St7789TftDisplayConfig
{
    std::uint16_t width              = 240;
    std::uint16_t height             = 240;
    std::uint8_t  rotation           = 0;
    std::uint8_t  backlight_gpio     = 5;
    std::uint8_t  backlight_on_level = LOW;
    bool          enable_backlight   = true;
    bool          swap_bytes         = true;
};

class St7789TftDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit St7789TftDisplay(St7789TftDisplayConfig config);
    bool                                    begin() override;
    brick::interfaces::display::DisplaySize size() const override;
    brick::interfaces::display::PixelFormat pixel_format() const override;
    bool                                    set_rotation(brick::interfaces::display::Rotation rotation) override;
    bool                                    draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height, const std::uint8_t* pixels, std::size_t byte_count) override;

private:
    St7789TftDisplayConfig config_;
    TFT_eSPI               tft_;
    bool                   initialized_ = false;
};

}  // namespace brick::platform::esp8266
