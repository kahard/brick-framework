#pragma once

#include "brick/interfaces/display/DisplayTypes.h"
#include "brick/platform/esp32/touch/Gsl3680Touchscreen.h"

namespace brick::platform::esp32::p4::profiles
{

inline touch::Gsl3680Config jc8012_gsl3680(brick::interfaces::display::Rotation rotation = brick::interfaces::display::Rotation::rotate_0)
{
    touch::Gsl3680Config config;
    const bool swapped = rotation == brick::interfaces::display::Rotation::rotate_90 || rotation == brick::interfaces::display::Rotation::rotate_270;
    const bool mirrored_x = rotation == brick::interfaces::display::Rotation::rotate_0 || rotation == brick::interfaces::display::Rotation::rotate_270;
    const bool mirrored_y = rotation == brick::interfaces::display::Rotation::rotate_180 || rotation == brick::interfaces::display::Rotation::rotate_270;
    config.sda_gpio = GPIO_NUM_7;
    config.scl_gpio = GPIO_NUM_8;
    config.reset_gpio = GPIO_NUM_22;
    config.interrupt_gpio = GPIO_NUM_21;
    config.clock_hz = 100000;
    // x_max/y_max describe the controller's physical axes. They must not be
    // swapped for a rotated logical display; esp_lcd_touch applies mirror_x
    // first and swap_xy afterwards.
    config.display_size = { 800, 1280 };
    // GSL3680 reports the physical X axis reversed (x=0 is the right side).
    // The touch driver applies mirror_x/mirror_y before swap_xy. Select the
    // equivalent transform for each logical display rotation.
    config.mirror_x = mirrored_x;
    config.mirror_y = mirrored_y;
    config.swap_xy = swapped;
    return config;
}

}  // namespace brick::platform::esp32::p4::profiles
