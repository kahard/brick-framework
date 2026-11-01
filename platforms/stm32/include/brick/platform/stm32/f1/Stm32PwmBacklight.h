#pragma once

#include <cstdint>

#include "brick/interfaces/display/IBacklight.h"
#include "brick/platform/stm32/f1/Gpio.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct Stm32PwmBacklightConfig
{
    TIM_TypeDef*  timer   = nullptr;
    std::uint32_t channel = TIM_CHANNEL_1;
    GpioPin       pin;
    /// Timer clock divider minus one: 71 counts at 1 MHz from a 72 MHz timer clock.
    std::uint32_t prescaler = 71;
    /// Counts per PWM period minus one: 999 gives 1 kHz at a 1 MHz count.
    std::uint32_t period      = 999;
    bool          active_high = true;
};

/// Backlight dimmed by a timer PWM channel. A pin remap (AFIO) the channel
/// needs is set up by the board before begin().
class Stm32PwmBacklight final : public brick::interfaces::display::IBacklight
{
public:
    explicit Stm32PwmBacklight(Stm32PwmBacklightConfig config) : config_(config) {}

    Status       begin();
    Status       set_enabled(bool enabled);
    Status       set_brightness_percent(std::uint8_t percent) override;
    std::uint8_t brightness_percent() const override { return brightness_; }

private:
    void set_pulse_(std::uint8_t percent);

    Stm32PwmBacklightConfig config_;
    TIM_HandleTypeDef       timer_{};
    std::uint8_t            brightness_  = 100;
    bool                    initialized_ = false;
};

}  // namespace brick::platform::stm32::f1
