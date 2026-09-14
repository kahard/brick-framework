#pragma once

#include "brick/platform/esp32/SdSpiFileSystem.h"
#include "brick/platform/esp32/wroom32/CydSpi3PinMux.h"

namespace brick::platform::esp32::profiles
{

inline wroom32::CydSpi3PinMuxConfig cyd_spi3_pin_mux()
{
    wroom32::CydSpi3PinMuxConfig config{};
    config.touch_sclk      = GPIO_NUM_25;
    config.touch_mosi      = GPIO_NUM_32;
    config.touch_miso      = GPIO_NUM_39;
    config.touch_interrupt = GPIO_NUM_36;
    config.sd_sclk         = GPIO_NUM_18;
    config.sd_mosi         = GPIO_NUM_23;
    config.sd_miso         = GPIO_NUM_19;
    return config;
}

inline SdSpiFileSystemConfig cyd_sd_spi()
{
    SdSpiFileSystemConfig config{};
    config.cs           = GPIO_NUM_5;
    config.sck          = GPIO_NUM_18;
    config.mosi         = GPIO_NUM_23;
    config.miso         = GPIO_NUM_19;
    config.host         = SPI3_HOST;
    config.max_freq_khz = 10000;
    return config;
}

}  // namespace brick::platform::esp32::profiles
