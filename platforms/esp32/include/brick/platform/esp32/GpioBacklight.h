#pragma once

#include <cstdint>

#include "brick/interfaces/display/IBacklight.h"
#include "driver/gpio.h"

namespace brick::platform::esp32
{

/// On/off backlight driven by a plain GPIO. Any non-zero brightness turns the
/// backlight fully on; zero turns it off.
class GpioBacklight final : public brick::interfaces::display::IBacklight
{
public:
    explicit GpioBacklight(gpio_num_t gpio, bool active_high = true) : gpio_(gpio), active_high_(active_high) {}

    bool begin(std::uint8_t initial_brightness_percent = 100U)
    {
        if (gpio_set_direction(gpio_, GPIO_MODE_OUTPUT) != ESP_OK)
            return false;
        initialized_ = true;
        return set_brightness_percent(initial_brightness_percent);
    }

    bool set_brightness_percent(std::uint8_t percent) override
    {
        if (!initialized_ || percent > 100U)
            return false;
        const bool on = percent > 0U;
        if (gpio_set_level(gpio_, on == active_high_ ? 1U : 0U) != ESP_OK)
            return false;
        brightness_percent_ = on ? 100U : 0U;
        return true;
    }

    std::uint8_t brightness_percent() const override { return brightness_percent_; }

private:
    gpio_num_t   gpio_;
    bool         active_high_;
    std::uint8_t brightness_percent_ = 0U;
    bool         initialized_        = false;
};

}  // namespace brick::platform::esp32
