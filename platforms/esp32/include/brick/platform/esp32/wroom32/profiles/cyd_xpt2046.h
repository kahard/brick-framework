#pragma once

#include "brick/platform/esp32/touch/Xpt2046Touchscreen.h"

namespace brick::platform::esp32::profiles
{

inline touch::Xpt2046Config cyd_xpt2046()
{
    touch::Xpt2046Config config{};
    config.spi_host              = SPI3_HOST;
    config.sclk_gpio             = GPIO_NUM_25;
    config.mosi_gpio             = GPIO_NUM_32;
    config.miso_gpio             = GPIO_NUM_39;
    config.cs_gpio               = GPIO_NUM_33;
    config.interrupt_gpio        = GPIO_NUM_36;
    config.spi_clock_hz          = 2'000'000;
    config.threshold             = 100;
    config.display_size          = { 320, 240 };
    config.calibration.raw_x_min = 428;
    config.calibration.raw_x_max = 3582;
    config.calibration.raw_y_min = 316;
    config.calibration.raw_y_max = 3754;
    config.calibration.swap_xy   = true;
    config.calibration.invert_x  = true;
    config.calibration.invert_y  = true;
    return config;
}

}  // namespace brick::platform::esp32::profiles
