#pragma once

#include "brick/boards/esp32/p4/jc8012/InitSequence.h"
#include "brick/interfaces/display/DisplayTypes.h"
#include "brick/platform/esp32/p4/MipiDsiDisplay.h"
#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#include "brick/platform/esp32/touch/Gsl3680Touchscreen.h"
#include "driver/gpio.h"

// Guition JC8012P4A1C: ESP32-P4, 10-inch 800x1280 MIPI-DSI panel (JD9365),
// GSL3680 touch and a 4-bit SDMMC card slot.
namespace brick::boards::jc8012
{

using brick::interfaces::display::Rotation;

inline constexpr gpio_num_t kBacklightGpio = GPIO_NUM_23;

inline brick::platform::esp32::p4::MipiDsiPanelConfig display_config(Rotation rotation = Rotation::rotate_0)
{
    brick::platform::esp32::p4::MipiDsiPanelConfig config;
    config.width                   = 800;
    config.height                  = 1280;
    config.data_lanes              = 2;
    config.lane_bit_rate_mbps      = 1500.0f;
    config.pixel_clock_mhz         = 60.0f;
    config.hsync_pulse_width       = 20;
    config.hsync_back_porch        = 20;
    config.hsync_front_porch       = 40;
    config.vsync_pulse_width       = 4;
    config.vsync_back_porch        = 8;
    config.vsync_front_porch       = 20;
    config.reset_gpio              = GPIO_NUM_27;
    config.mipi_dsi_ldo_channel    = 3;
    config.mipi_dsi_ldo_voltage_mv = 2500;
    config.default_rotation        = rotation;
    config.init_sequence           = kInitSequence;
    config.init_sequence_size      = sizeof(kInitSequence);
    return config;
}

inline brick::platform::esp32::touch::Gsl3680Config touch_config(Rotation rotation = Rotation::rotate_0)
{
    brick::platform::esp32::touch::Gsl3680Config config;
    const bool swapped    = rotation == Rotation::rotate_90 || rotation == Rotation::rotate_270;
    const bool mirrored_x = rotation == Rotation::rotate_0 || rotation == Rotation::rotate_270;
    const bool mirrored_y = rotation == Rotation::rotate_180 || rotation == Rotation::rotate_270;
    config.sda_gpio       = GPIO_NUM_7;
    config.scl_gpio       = GPIO_NUM_8;
    config.reset_gpio     = GPIO_NUM_22;
    config.interrupt_gpio = GPIO_NUM_21;
    config.clock_hz       = 100000;
    // x_max/y_max describe the controller's physical axes. They must not be
    // swapped for a rotated logical display; esp_lcd_touch applies mirror_x
    // first and swap_xy afterwards.
    config.display_size = { 800, 1280 };
    // GSL3680 reports the physical X axis reversed (x=0 is the right side).
    // The touch driver applies mirror_x/mirror_y before swap_xy. Select the
    // equivalent transform for each logical display rotation.
    config.mirror_x = mirrored_x;
    config.mirror_y = mirrored_y;
    config.swap_xy  = swapped;
    return config;
}

inline brick::platform::esp32::SdmmcFileSystemConfig sd_config()
{
    brick::platform::esp32::SdmmcFileSystemConfig config;
    config.clk          = GPIO_NUM_43;
    config.cmd          = GPIO_NUM_44;
    config.d0           = GPIO_NUM_39;
    config.d1           = GPIO_NUM_40;
    config.d2           = GPIO_NUM_41;
    config.d3           = GPIO_NUM_42;
    config.max_freq_khz = 10'000;
    config.bus_width    = 4;
    config.host_slot    = SDMMC_HOST_SLOT_0;
    return config;
}

}  // namespace brick::boards::jc8012
