#pragma once

#include "brick/platform/stm32/f1/ResistiveTouchscreen.h"
#include "brick/platform/stm32/f1/Ssd1963ParallelDisplay.h"

namespace brick::platform::stm32::f1::profiles
{

inline Ssd1963ParallelDisplayConfig st280_480x272()
{
    return {};
}

inline ResistiveTouchscreenConfig st280_resistive_touch()
{
    return {};
}

}  // namespace brick::platform::stm32::f1::profiles
