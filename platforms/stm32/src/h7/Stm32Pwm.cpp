#include "brick/platform/stm32/h7/Stm32Pwm.h"

#include <algorithm>

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

namespace
{
    constexpr std::uint32_t kFullDuty = 1000;
}  // namespace

Status Stm32Pwm::begin()
{
    if (started_)
        return Status::ok;
    if (config_.timer == nullptr || !config_.pin.pin.connected())
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.timer); !is_ok(status))
        return status;
    configure(config_.pin, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW);
    timer_.Instance               = config_.timer;
    timer_.Init.Prescaler         = config_.prescaler;
    timer_.Init.CounterMode       = TIM_COUNTERMODE_UP;
    timer_.Init.Period            = config_.period;
    timer_.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    timer_.Init.RepetitionCounter = 0;
    timer_.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
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
    started_ = true;
    return Status::ok;
}

void Stm32Pwm::set_duty_permille(std::uint32_t permille)
{
    if (!started_)
        return;
    const std::uint32_t counts = (config_.period + 1U) * std::min(permille, kFullDuty) / kFullDuty;
    __HAL_TIM_SET_COMPARE(&timer_, config_.channel, counts);
}

}  // namespace brick::platform::stm32::h7
