#pragma once

#include "brick/platform/esp8266/GpioButton.h"
#include "brick/platform/esp8266/St7789TftDisplay.h"

namespace brick::platform::esp8266::profiles
{

inline St7789TftDisplayConfig esp12f_st7789_240x240()
{
    St7789TftDisplayConfig config{};
    config.width              = 240;
    config.height             = 240;
    config.rotation           = 0;
    config.backlight_gpio     = 5;
    config.backlight_on_level = LOW;
    config.swap_bytes         = true;
    return config;
}

inline GpioButtonConfig esp12f_ttp223_gpio4()
{
    GpioButtonConfig config{};
    config.gpio         = 4;
    config.pin_mode     = INPUT;
    config.active_level = HIGH;
    return config;
}

}  // namespace brick::platform::esp8266::profiles
