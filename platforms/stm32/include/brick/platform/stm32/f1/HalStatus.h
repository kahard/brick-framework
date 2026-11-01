#pragma once

#include "brick/Status.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

/// The Status closest to an STM32 HAL result.
constexpr Status to_status(HAL_StatusTypeDef result)
{
    switch (result)
    {
        case HAL_OK:
            return Status::ok;
        case HAL_BUSY:
            return Status::busy;
        case HAL_TIMEOUT:
            return Status::timeout;
        default:
            return Status::error;
    }
}

}  // namespace brick::platform::stm32::f1
