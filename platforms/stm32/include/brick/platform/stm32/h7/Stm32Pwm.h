#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/platform/stm32/h7/Gpio.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

/// One PWM channel of a general-purpose timer.
struct Stm32PwmConfig
{
    TIM_TypeDef*  timer   = nullptr;
    std::uint32_t channel = TIM_CHANNEL_1;
    AlternatePin  pin;
    /// Timer clock divider minus one: 199 counts at 1 MHz from the 200 MHz
    /// clock of the APB1 timers.
    std::uint32_t prescaler = 199;
    /// Counts per PWM period minus one: 999 gives 1 kHz at a 1 MHz count.
    std::uint32_t period      = 999;
    bool          active_high = true;
};

/// A timer PWM output with a duty cycle in per mille.
class Stm32Pwm final
{
public:
    explicit Stm32Pwm(Stm32PwmConfig config) : config_(config) {}

    /// Starts the output at duty 0.
    Status begin();
    /// 0 (always off) .. 1000 (always on).
    void set_duty_permille(std::uint32_t permille);

private:
    Stm32PwmConfig    config_;
    TIM_HandleTypeDef timer_{};
    bool              started_ = false;
};

}  // namespace brick::platform::stm32::h7
