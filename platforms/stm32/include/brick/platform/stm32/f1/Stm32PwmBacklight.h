#pragma once

#include <cstdint>

#include "brick/interfaces/display/IBacklight.h"
#include "brick/platform/stm32/f1/HalStatus.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct Stm32PwmBacklightConfig
{
    TIM_HandleTypeDef* timer        = nullptr;
    GPIO_TypeDef*      port         = GPIOC;
    std::uint16_t      pin          = GPIO_PIN_8;
    std::uint32_t      channel      = TIM_CHANNEL_3;
    std::uint32_t      timer_period = 999;
    bool               active_high  = true;
};

class Stm32PwmBacklight final : public brick::interfaces::display::IBacklight
{
public:
    explicit Stm32PwmBacklight(Stm32PwmBacklightConfig config = {}) : config_(config) {}

    Status       begin();
    Status       set_enabled(bool enabled);
    Status       set_brightness_percent(std::uint8_t percent) override;
    std::uint8_t brightness_percent() const override { return brightness_; }

private:
    Stm32PwmBacklightConfig config_;
    std::uint8_t            brightness_  = 100;
    bool                    initialized_ = false;
};

}  // namespace brick::platform::stm32::f1
