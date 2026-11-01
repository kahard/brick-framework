#pragma once

#include "brick/Status.h"
#include "stm32f1xx_hal.h"

// Clock enables for the peripheral instances of the STM32F1 family, so a
// driver configured with an instance can start it without knowing which one
// a board chose. Each returns Status::not_supported for an unknown instance.
namespace brick::platform::stm32::f1
{

inline Status enable_clock(GPIO_TypeDef* port)
{
    if (port == GPIOA)
        __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB)
        __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC)
        __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD)
        __HAL_RCC_GPIOD_CLK_ENABLE();
#if defined(GPIOE)
    else if (port == GPIOE)
        __HAL_RCC_GPIOE_CLK_ENABLE();
#endif
    else
        return Status::not_supported;
    return Status::ok;
}

inline Status enable_clock(ADC_TypeDef* adc)
{
    if (adc == ADC1)
        __HAL_RCC_ADC1_CLK_ENABLE();
#if defined(ADC2)
    else if (adc == ADC2)
        __HAL_RCC_ADC2_CLK_ENABLE();
#endif
    else
        return Status::not_supported;
    return Status::ok;
}

inline Status enable_clock(TIM_TypeDef* timer)
{
    if (timer == TIM1)
        __HAL_RCC_TIM1_CLK_ENABLE();
    else if (timer == TIM2)
        __HAL_RCC_TIM2_CLK_ENABLE();
    else if (timer == TIM3)
        __HAL_RCC_TIM3_CLK_ENABLE();
#if defined(TIM4)
    else if (timer == TIM4)
        __HAL_RCC_TIM4_CLK_ENABLE();
#endif
#if defined(TIM5)
    else if (timer == TIM5)
        __HAL_RCC_TIM5_CLK_ENABLE();
#endif
    else
        return Status::not_supported;
    return Status::ok;
}

inline Status enable_clock(USART_TypeDef* uart)
{
    if (uart == USART1)
        __HAL_RCC_USART1_CLK_ENABLE();
    else if (uart == USART2)
        __HAL_RCC_USART2_CLK_ENABLE();
#if defined(USART3)
    else if (uart == USART3)
        __HAL_RCC_USART3_CLK_ENABLE();
#endif
#if defined(UART4)
    else if (uart == UART4)
        __HAL_RCC_UART4_CLK_ENABLE();
#endif
#if defined(UART5)
    else if (uart == UART5)
        __HAL_RCC_UART5_CLK_ENABLE();
#endif
    else
        return Status::not_supported;
    return Status::ok;
}

inline Status enable_clock(I2C_TypeDef* i2c)
{
    if (i2c == I2C1)
        __HAL_RCC_I2C1_CLK_ENABLE();
#if defined(I2C2)
    else if (i2c == I2C2)
        __HAL_RCC_I2C2_CLK_ENABLE();
#endif
    else
        return Status::not_supported;
    return Status::ok;
}

inline Status enable_clock(SPI_TypeDef* spi)
{
    if (spi == SPI1)
        __HAL_RCC_SPI1_CLK_ENABLE();
#if defined(SPI2)
    else if (spi == SPI2)
        __HAL_RCC_SPI2_CLK_ENABLE();
#endif
#if defined(SPI3)
    else if (spi == SPI3)
        __HAL_RCC_SPI3_CLK_ENABLE();
#endif
    else
        return Status::not_supported;
    return Status::ok;
}

}  // namespace brick::platform::stm32::f1
