#include "brick/platform/stm32/f1/Stm32PwmBacklight.h"

namespace brick::platform::stm32::f1
{

bool Stm32PwmBacklight::begin()
{
    if (config_.timer == nullptr)
        return false;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_AFIO_REMAP_TIM3_ENABLE();
    GPIO_InitTypeDef pin{};
    pin.Pin = config_.pin;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(config_.port, &pin);
    config_.timer->Instance = TIM3;
    config_.timer->Init.Prescaler = 71;
    config_.timer->Init.CounterMode = TIM_COUNTERMODE_UP;
    config_.timer->Init.Period = config_.timer_period;
    config_.timer->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    config_.timer->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(config_.timer) != HAL_OK)
        return false;
    TIM_OC_InitTypeDef channel{};
    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = 0;
    channel.OCPolarity = config_.active_high ? TIM_OCPOLARITY_HIGH : TIM_OCPOLARITY_LOW;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(config_.timer, &channel, config_.channel) != HAL_OK)
        return false;
    if (HAL_TIM_PWM_Start(config_.timer, config_.channel) != HAL_OK)
        return false;
    initialized_ = true;
    return set_brightness(brightness_);
}

bool Stm32PwmBacklight::set_enabled(bool enabled)
{
    return set_brightness(enabled ? brightness_ : 0);
}

bool Stm32PwmBacklight::set_brightness(std::uint8_t percent)
{
    if (!initialized_ && config_.timer == nullptr)
        return false;
    if (percent > 100)
        percent = 100;
    brightness_ = percent;
    const auto pulse = static_cast<std::uint32_t>(config_.timer_period) * percent / 100u;
    __HAL_TIM_SET_COMPARE(config_.timer, config_.channel, pulse);
    return true;
}

}  // namespace brick::platform::stm32::f1
