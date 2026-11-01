#pragma once

#include <cstdint>

#include "brick/interfaces/display/IBacklight.h"
#include "driver/ledc.h"

namespace brick::platform::esp32
{

class PwmBacklight final : public brick::interfaces::display::IBacklight
{
public:
    explicit PwmBacklight(int gpio_num) : gpio_num_(gpio_num) {}

    Status begin(std::uint8_t initial_brightness_percent = 100U)
    {
        if (initial_brightness_percent > 100U)
            return Status::invalid_argument;

        ledc_timer_config_t timer_config = {};
        timer_config.speed_mode          = LEDC_LOW_SPEED_MODE;
        timer_config.timer_num           = LEDC_TIMER_0;
        timer_config.duty_resolution     = LEDC_TIMER_10_BIT;
        timer_config.freq_hz             = 5000;
        timer_config.clk_cfg             = LEDC_AUTO_CLK;
        if (ledc_timer_config(&timer_config) != ESP_OK)
            return Status::error;

        ledc_channel_config_t channel_config = {};
        channel_config.gpio_num              = gpio_num_;
        channel_config.speed_mode            = LEDC_LOW_SPEED_MODE;
        channel_config.channel               = LEDC_CHANNEL_0;
        channel_config.intr_type             = LEDC_INTR_DISABLE;
        channel_config.timer_sel             = LEDC_TIMER_0;
        channel_config.duty                  = duty_from_percent(initial_brightness_percent);
        channel_config.hpoint                = 0;
        if (ledc_channel_config(&channel_config) != ESP_OK)
            return Status::error;

        brightness_percent_ = initial_brightness_percent;
        initialized_        = true;
        return Status::ok;
    }

    Status set_brightness_percent(std::uint8_t percent) override
    {
        if (!initialized_)
            return Status::not_ready;
        if (percent > 100U)
            return Status::invalid_argument;
        if (ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_from_percent(percent)) != ESP_OK)
            return Status::error;
        if (ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) != ESP_OK)
            return Status::error;
        brightness_percent_ = percent;
        return Status::ok;
    }

    std::uint8_t brightness_percent() const override { return brightness_percent_; }

private:
    static std::uint32_t duty_from_percent(std::uint8_t percent) { return (1023U * percent) / 100U; }

    int          gpio_num_;
    std::uint8_t brightness_percent_ = 100U;
    bool         initialized_        = false;
};

}  // namespace brick::platform::esp32
