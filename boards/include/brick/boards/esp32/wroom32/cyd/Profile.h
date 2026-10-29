#pragma once

#include "brick/platform/esp32/Ili9341SpiDisplay.h"
#include "brick/platform/esp32/SdSpiFileSystem.h"
#include "brick/platform/esp32/touch/Xpt2046Touchscreen.h"
#include "brick/platform/esp32/wroom32/CydSpi3PinMux.h"
#include "driver/gpio.h"

// ESP32-2432S028R "Cheap Yellow Display": ESP32-WROOM, 2.8-inch 320x240
// ILI9341 SPI display, XPT2046 resistive touch and an SPI SD card slot. Touch
// and SD share the SPI3 host and are switched by CydSpi3PinMux.
namespace brick::boards::cyd
{

inline constexpr gpio_num_t kBacklightGpio = GPIO_NUM_21;

inline brick::platform::esp32::Ili9341SpiDisplayConfig display_config()
{
    brick::platform::esp32::Ili9341SpiDisplayConfig config{};
    config.width          = 320;
    config.height         = 240;
    config.spi_host       = SPI2_HOST;
    config.sclk_gpio      = GPIO_NUM_14;
    config.mosi_gpio      = GPIO_NUM_13;
    config.miso_gpio      = GPIO_NUM_12;
    config.cs_gpio        = GPIO_NUM_15;
    config.dc_gpio        = GPIO_NUM_2;
    config.backlight_gpio = kBacklightGpio;
    config.spi_mode       = 0;
    config.spi_clock_hz   = 80'000'000;
    config.bgr            = false;
    config.madctl         = 0x40;
    return config;
}

inline brick::platform::esp32::touch::Xpt2046Config touch_config()
{
    brick::platform::esp32::touch::Xpt2046Config config{};
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

inline brick::platform::esp32::wroom32::CydSpi3PinMuxConfig spi3_pin_mux_config()
{
    brick::platform::esp32::wroom32::CydSpi3PinMuxConfig config{};
    config.touch_sclk      = GPIO_NUM_25;
    config.touch_mosi      = GPIO_NUM_32;
    config.touch_miso      = GPIO_NUM_39;
    config.touch_interrupt = GPIO_NUM_36;
    config.sd_sclk         = GPIO_NUM_18;
    config.sd_mosi         = GPIO_NUM_23;
    config.sd_miso         = GPIO_NUM_19;
    return config;
}

inline brick::platform::esp32::SdSpiFileSystemConfig sd_config()
{
    brick::platform::esp32::SdSpiFileSystemConfig config{};
    config.cs           = GPIO_NUM_5;
    config.sck          = GPIO_NUM_18;
    config.mosi         = GPIO_NUM_23;
    config.miso         = GPIO_NUM_19;
    config.host         = SPI3_HOST;
    config.max_freq_khz = 10000;
    return config;
}

}  // namespace brick::boards::cyd
