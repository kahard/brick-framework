#pragma once

#include <cstdint>

#include "brick/platform/stm32/f1/Gpio.h"

namespace brick::platform::stm32::f1
{

/// Kept apart from Stm32UsbHost.h, which needs the ST USB host library, so
/// a board profile can describe the port without it.
struct Stm32UsbHostConfig
{
    /// VBUS switch, high = on; not connected when VBUS is always on.
    GpioPin      power;
    std::uint8_t host_id = 0;
};

}  // namespace brick::platform::stm32::f1
