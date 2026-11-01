#pragma once

#include <cstdint>

#include "brick/interfaces/time/ITimeProvider.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

/// Time from the HAL tick (1 ms, SysTick). Stm32HalTime.cpp defines
/// SysTick_Handler, so an application using this class does not.
class Stm32HalTime final : public brick::interfaces::time::ITimeProvider
{
public:
    std::uint32_t millis() const override { return HAL_GetTick(); }
    void          delay_ms(std::uint32_t milliseconds) override { HAL_Delay(milliseconds); }
};

}  // namespace brick::platform::stm32::f1
