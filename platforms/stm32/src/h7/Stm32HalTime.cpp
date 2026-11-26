#include "brick/platform/stm32/h7/Stm32HalTime.h"

// The HAL time base behind HAL_GetTick() and HAL_Delay().
extern "C" void SysTick_Handler()
{
    HAL_IncTick();
}
