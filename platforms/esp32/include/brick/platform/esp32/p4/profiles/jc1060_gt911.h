#pragma once

#include "brick/platform/esp32/touch/Gt911Touchscreen.h"

namespace brick::platform::esp32::p4::profiles
{

inline touch::Gt911Config jc1060_gt911()
{
    touch::Gt911Config config;
    config.port                             = I2C_NUM_0;
    config.sda_gpio                         = GPIO_NUM_7;
    config.scl_gpio                         = GPIO_NUM_8;
    config.display_size                     = { 1024, 600 };
    config.address                          = 0x5D;
    config.read_calibration_from_controller = true;
    return config;
}

}  // namespace brick::platform::esp32::p4::profiles
