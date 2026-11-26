#pragma once

#include <cstdint>

#include "brick/interfaces/display/IBacklight.h"
#include "brick/platform/stm32/h7/Stm32Pwm.h"

namespace brick::platform::stm32::h7
{

/// Backlight dimmed by a timer PWM channel driving the enable input of an
/// LED driver.
class Stm32PwmBacklight final : public brick::interfaces::display::IBacklight
{
public:
    explicit Stm32PwmBacklight(Stm32PwmConfig config) : pwm_(config) {}

    /// Starts the PWM at the last brightness (100 % at first).
    Status begin()
    {
        if (const Status status = pwm_.begin(); !is_ok(status))
            return status;
        started_ = true;
        return set_brightness_percent(brightness_);
    }

    /// Off keeps the brightness for the next set_enabled(true).
    Status set_enabled(bool enabled)
    {
        if (!started_)
            return Status::not_ready;
        pwm_.set_duty_permille(enabled ? brightness_ * 10U : 0U);
        return Status::ok;
    }

    Status set_brightness_percent(std::uint8_t percent) override
    {
        if (!started_)
            return Status::not_ready;
        if (percent > 100)
            return Status::invalid_argument;
        brightness_ = percent;
        pwm_.set_duty_permille(percent * 10U);
        return Status::ok;
    }

    std::uint8_t brightness_percent() const override { return brightness_; }

private:
    Stm32Pwm     pwm_;
    std::uint8_t brightness_ = 100;
    bool         started_    = false;
};

}  // namespace brick::platform::stm32::h7
