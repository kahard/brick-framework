#pragma once

#include <cstdint>

#include "brick/interfaces/audio/IBuzzer.h"
#include "brick/platform/stm32/h7/Stm32Pwm.h"

namespace brick::platform::stm32::h7
{

/// A buzzer without its own oscillator (a piezo or magnetic sounder), driven
/// by a timer PWM channel at its resonant frequency with a 50 % duty cycle
/// while on.
class Stm32PwmBuzzer final : public brick::interfaces::audio::IBuzzer
{
public:
    explicit Stm32PwmBuzzer(Stm32PwmConfig config) : pwm_(config) {}

    Status begin() override
    {
        if (const Status status = pwm_.begin(); !is_ok(status))
            return status;
        started_ = true;
        return set_enabled(false);
    }

    Status set_enabled(bool enabled) override
    {
        if (!started_)
            return Status::not_ready;
        pwm_.set_duty_permille(enabled ? kHalfDuty : 0U);
        return Status::ok;
    }

    /// Blocks for duration_ms; a loop that must not block uses set_enabled().
    Status beep(std::uint16_t duration_ms) override
    {
        if (const Status status = set_enabled(true); !is_ok(status))
            return status;
        HAL_Delay(duration_ms);
        return set_enabled(false);
    }

private:
    static constexpr std::uint32_t kHalfDuty = 500;

    Stm32Pwm pwm_;
    bool     started_ = false;
};

}  // namespace brick::platform::stm32::h7
