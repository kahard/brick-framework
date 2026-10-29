#pragma once

#include "brick/platform/esp8266/GpioButton.h"
#include "brick/platform/esp8266/St7789TftDisplay.h"

// ESP-12F (ESP8266) with a 240x240 ST7789 SPI display and a TTP223 capacitive
// touch button on GPIO4.
namespace brick::boards::esp12f_st7789
{

inline brick::platform::esp8266::St7789TftDisplayConfig display_config()
{
    brick::platform::esp8266::St7789TftDisplayConfig config{};
    config.width              = 240;
    config.height             = 240;
    config.rotation           = 0;
    config.backlight_gpio     = 5;
    config.backlight_on_level = LOW;
    config.swap_bytes         = true;
    return config;
}

inline brick::platform::esp8266::GpioButtonConfig button_config()
{
    brick::platform::esp8266::GpioButtonConfig config{};
    config.gpio         = 4;
    config.pin_mode     = INPUT;
    config.active_level = HIGH;
    return config;
}

}  // namespace brick::boards::esp12f_st7789
