#include "brick/platform/stm32/f1/Stm32Buzzer.h"

namespace brick::platform::stm32::f1
{

bool Stm32Buzzer::begin()
{
    if (config_.port == GPIOB)
        __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (config_.port == GPIOA)
        __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (config_.port == GPIOC)
        __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef pins{};
    pins.Pin = config_.pin;
    pins.Mode = GPIO_MODE_OUTPUT_PP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(config_.port, &pins);
    initialized_ = true;
    return set_enabled(false);
}

bool Stm32Buzzer::set_enabled(bool enabled)
{
    if (!initialized_)
        return false;
    const bool level = enabled == config_.active_high;
    if (level)
        config_.port->BSRR = config_.pin;
    else
        config_.port->BSRR = static_cast<std::uint32_t>(config_.pin) << 16;
    return true;
}

bool Stm32Buzzer::beep(std::uint16_t duration_ms)
{
    if (!set_enabled(true))
        return false;
    HAL_Delay(duration_ms);
    return set_enabled(false);
}

}  // namespace brick::platform::stm32::f1
