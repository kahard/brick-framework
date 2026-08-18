#pragma once

#include "brick/platform/esp32/touch/Gt911Touchscreen.h"

namespace brick::platform::esp32::s3::profiles
{

inline brick::platform::esp32::touch::Gt911Config st7701s_gt911()
{
    brick::platform::esp32::touch::Gt911Config config{};
    config.port         = I2C_NUM_0;
    config.sda_gpio     = GPIO_NUM_19;
    config.scl_gpio     = GPIO_NUM_45;
    config.clock_hz     = 100'000;
    config.display_size = { 480, 480 };
    return config;
}

}  // namespace brick::platform::esp32::s3::profiles
