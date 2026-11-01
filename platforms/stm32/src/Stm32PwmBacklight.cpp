#include "brick/platform/stm32/f1/Stm32PwmBacklight.h"

#include "brick/platform/stm32/f1/HalStatus.h"

namespace brick::platform::stm32::f1
{

Status Stm32PwmBacklight::begin()
{
    if (config_.timer == nullptr || !config_.pin.connected())
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.timer); !is_ok(status))
        return status;
    configure(config_.pin, GPIO_MODE_AF_PP);
    timer_.Instance               = config_.timer;
    timer_.Init.Prescaler         = config_.prescaler;
    timer_.Init.CounterMode       = TIM_COUNTERMODE_UP;
    timer_.Init.Period            = config_.period;
    timer_.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    timer_.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (const HAL_StatusTypeDef result = HAL_TIM_PWM_Init(&timer_); result != HAL_OK)
        return to_status(result);
    TIM_OC_InitTypeDef channel{};
    channel.OCMode     = TIM_OCMODE_PWM1;
    channel.Pulse      = 0;
    channel.OCPolarity = config_.active_high ? TIM_OCPOLARITY_HIGH : TIM_OCPOLARITY_LOW;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    if (const HAL_StatusTypeDef result = HAL_TIM_PWM_ConfigChannel(&timer_, &channel, config_.channel);
        result != HAL_OK)
        return to_status(result);
    if (const HAL_StatusTypeDef result = HAL_TIM_PWM_Start(&timer_, config_.channel); result != HAL_OK)
        return to_status(result);
    initialized_ = true;
    return set_brightness_percent(brightness_);
}

Status Stm32PwmBacklight::set_enabled(bool enabled)
{
    if (!initialized_)
        return Status::not_ready;
    // Disabling keeps the brightness for the next set_enabled(true).
    set_pulse_(enabled ? brightness_ : 0U);
    return Status::ok;
}

Status Stm32PwmBacklight::set_brightness_percent(std::uint8_t percent)
{
    if (!initialized_)
        return Status::not_ready;
    if (percent > 100)
        return Status::invalid_argument;
    brightness_ = percent;
    set_pulse_(percent);
    return Status::ok;
}

void Stm32PwmBacklight::set_pulse_(std::uint8_t percent)
{
    __HAL_TIM_SET_COMPARE(&timer_, config_.channel, config_.period * percent / 100U);
}

}  // namespace brick::platform::stm32::f1
