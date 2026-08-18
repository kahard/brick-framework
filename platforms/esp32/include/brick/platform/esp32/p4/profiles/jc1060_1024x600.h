#pragma once

#include "brick/platform/esp32/p4/MipiDsiDisplay.h"

namespace brick::platform::esp32::p4::profiles
{

inline MipiDsiPanelConfig jc1060_1024x600(const std::uint8_t* init_sequence = nullptr, std::size_t init_sequence_size = 0)
{
    MipiDsiPanelConfig config;
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
    config.init_sequence           = init_sequence;
    config.init_sequence_size      = init_sequence_size;
    return config;
}

}  // namespace brick::platform::esp32::p4::profiles
