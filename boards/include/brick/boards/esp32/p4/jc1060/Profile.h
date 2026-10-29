#pragma once

#include "brick/boards/esp32/p4/jc1060/InitSequence.h"
#include "brick/platform/esp32/p4/MipiDsiDisplay.h"
#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#include "brick/platform/esp32/touch/Gt911Touchscreen.h"
#include "driver/gpio.h"

// Guition JC1060P470C-I-W: ESP32-P4, 7-inch 1024x600 MIPI-DSI panel (JD9165),
// GT911 touch and a 4-bit SDMMC card slot.
namespace brick::boards::jc1060
{

inline constexpr gpio_num_t kBacklightGpio = GPIO_NUM_23;

inline brick::platform::esp32::p4::MipiDsiPanelConfig display_config()
{
    brick::platform::esp32::p4::MipiDsiPanelConfig config;
    config.width                   = 1024;
    config.height                  = 600;
    config.data_lanes              = 2;
    config.lane_bit_rate_mbps      = 600.0f;
    config.pixel_clock_mhz         = 40.0f;
    config.hsync_pulse_width       = 20;
    config.hsync_back_porch        = 160;
    config.hsync_front_porch       = 160;
    config.vsync_pulse_width       = 10;
    config.vsync_back_porch        = 23;
    config.vsync_front_porch       = 12;
    config.reset_gpio              = GPIO_NUM_27;
    config.mipi_dsi_ldo_channel    = 3;
    config.mipi_dsi_ldo_voltage_mv = 2500;
    config.init_sequence           = kInitSequence;
    config.init_sequence_size      = sizeof(kInitSequence);
    return config;
}

inline brick::platform::esp32::touch::Gt911Config touch_config()
{
    brick::platform::esp32::touch::Gt911Config config;
    config.port                             = I2C_NUM_0;
    config.sda_gpio                         = GPIO_NUM_7;
    config.scl_gpio                         = GPIO_NUM_8;
    config.display_size                     = { 1024, 600 };
    config.address                          = 0x5D;
    config.read_calibration_from_controller = true;
    return config;
}

inline brick::platform::esp32::SdmmcFileSystemConfig sd_config()
{
    return {};
}

}  // namespace brick::boards::jc1060
