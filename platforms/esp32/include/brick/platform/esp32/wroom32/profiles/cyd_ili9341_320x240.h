#pragma once

#include "brick/platform/esp32/Ili9341SpiDisplay.h"

namespace brick::platform::esp32::profiles
{

inline Ili9341SpiDisplayConfig cyd_ili9341_320x240()
{
    Ili9341SpiDisplayConfig config{};
    config.width          = 320;
    config.height         = 240;
    config.spi_host       = SPI2_HOST;
    config.sclk_gpio      = GPIO_NUM_14;
    config.mosi_gpio      = GPIO_NUM_13;
    config.miso_gpio      = GPIO_NUM_12;
    config.cs_gpio        = GPIO_NUM_15;
    config.dc_gpio        = GPIO_NUM_2;
    config.backlight_gpio = GPIO_NUM_21;
    config.spi_mode       = 0;
    config.spi_clock_hz   = 40'000'000;
    config.bgr            = false;
    config.madctl         = 0x40;
    return config;
}

}  // namespace brick::platform::esp32::profiles
