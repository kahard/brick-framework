#pragma once

#include <cstdint>

#include "brick/platform/stm32/f1/Clocks.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

/// One pin of a GPIO port. A default-constructed pin is not connected, and
/// configure() and write() skip it, so optional pins need no special case.
struct GpioPin
{
    GPIO_TypeDef* port = nullptr;
    std::uint16_t pin  = 0;

    constexpr bool connected() const { return port != nullptr && pin != 0; }
};

/// Enables the port clock and configures the pin.
inline void configure(const GpioPin& pin, std::uint32_t mode, std::uint32_t pull = GPIO_NOPULL)
{
    if (!pin.connected() || !is_ok(enable_clock(pin.port)))
        return;
    GPIO_InitTypeDef init{};
    init.Pin   = pin.pin;
    init.Mode  = mode;
    init.Pull  = pull;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(pin.port, &init);
}

/// Drives the pin through BSRR, which sets or resets it in one write.
inline void write(const GpioPin& pin, bool high)
{
    if (!pin.connected())
        return;
    pin.port->BSRR = high ? pin.pin : static_cast<std::uint32_t>(pin.pin) << 16;
}

}  // namespace brick::platform::stm32::f1
