#pragma once

#include "brick/platform/stm32/f1/Ft5x06Touchscreen.h"
#include "brick/platform/stm32/f1/Ssd1963ParallelDisplay.h"

namespace brick::platform::stm32::f1::profiles
{

inline Ssd1963ParallelDisplayConfig st280_480x272()
{
    return {};
}

inline Ft5x06TouchscreenConfig st280_ft5x06()
{
    return {};
}

}  // namespace brick::platform::stm32::f1::profiles
