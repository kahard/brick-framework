#pragma once

#include "brick/Status.h"
#include "stm32h7xx_hal.h"

// Clock enables for the peripheral instances of the STM32H7 family, so a
// driver configured with an instance can start it without knowing which one
// a board chose. Each returns Status::not_supported for an unknown instance.
namespace brick::platform::stm32::h7
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
    else if (port == GPIOE)
        __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF)
        __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG)
        __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH)
        __HAL_RCC_GPIOH_CLK_ENABLE();
#if defined(GPIOI)
    else if (port == GPIOI)
        __HAL_RCC_GPIOI_CLK_ENABLE();
#endif
#if defined(GPIOJ)
    else if (port == GPIOJ)
        __HAL_RCC_GPIOJ_CLK_ENABLE();
#endif
#if defined(GPIOK)
    else if (port == GPIOK)
        __HAL_RCC_GPIOK_CLK_ENABLE();
#endif
    else
        return Status::not_supported;
    return Status::ok;
}

inline Status enable_clock(I2C_TypeDef* i2c)
{
    if (i2c == I2C1)
        __HAL_RCC_I2C1_CLK_ENABLE();
    else if (i2c == I2C2)
        __HAL_RCC_I2C2_CLK_ENABLE();
    else if (i2c == I2C3)
        __HAL_RCC_I2C3_CLK_ENABLE();
    else if (i2c == I2C4)
        __HAL_RCC_I2C4_CLK_ENABLE();
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
    else if (timer == TIM4)
        __HAL_RCC_TIM4_CLK_ENABLE();
    else if (timer == TIM5)
        __HAL_RCC_TIM5_CLK_ENABLE();
    else if (timer == TIM8)
        __HAL_RCC_TIM8_CLK_ENABLE();
    else if (timer == TIM12)
        __HAL_RCC_TIM12_CLK_ENABLE();
    else if (timer == TIM13)
        __HAL_RCC_TIM13_CLK_ENABLE();
    else if (timer == TIM14)
        __HAL_RCC_TIM14_CLK_ENABLE();
    else if (timer == TIM15)
        __HAL_RCC_TIM15_CLK_ENABLE();
    else if (timer == TIM16)
        __HAL_RCC_TIM16_CLK_ENABLE();
    else if (timer == TIM17)
        __HAL_RCC_TIM17_CLK_ENABLE();
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
    else if (uart == USART3)
        __HAL_RCC_USART3_CLK_ENABLE();
    else if (uart == UART4)
        __HAL_RCC_UART4_CLK_ENABLE();
    else if (uart == UART5)
        __HAL_RCC_UART5_CLK_ENABLE();
    else if (uart == USART6)
        __HAL_RCC_USART6_CLK_ENABLE();
    else if (uart == UART7)
        __HAL_RCC_UART7_CLK_ENABLE();
    else if (uart == UART8)
        __HAL_RCC_UART8_CLK_ENABLE();
    else
        return Status::not_supported;
    return Status::ok;
}

inline Status enable_clock(SPI_TypeDef* spi)
{
    if (spi == SPI1)
        __HAL_RCC_SPI1_CLK_ENABLE();
    else if (spi == SPI2)
        __HAL_RCC_SPI2_CLK_ENABLE();
    else if (spi == SPI3)
        __HAL_RCC_SPI3_CLK_ENABLE();
    else if (spi == SPI4)
        __HAL_RCC_SPI4_CLK_ENABLE();
    else if (spi == SPI5)
        __HAL_RCC_SPI5_CLK_ENABLE();
    else if (spi == SPI6)
        __HAL_RCC_SPI6_CLK_ENABLE();
    else
        return Status::not_supported;
    return Status::ok;
}

}  // namespace brick::platform::stm32::h7
