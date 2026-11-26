#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/platform/stm32/h7/Clocks.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

/// One pin of a GPIO port. A default-constructed pin is not connected, and
/// configure() and write() skip it, so optional pins need no special case.
struct GpioPin
{
    GPIO_TypeDef* port = nullptr;
    std::uint16_t pin  = 0;

    constexpr bool connected() const { return port != nullptr && pin != 0; }
};

/// A pin driven by a peripheral, with its alternate function number from the
/// datasheet (GPIO_AFx_...).
struct AlternatePin
{
    GpioPin      pin;
    std::uint8_t alternate = 0;
};

/// Enables the port clock and configures the pin.
inline void configure(const GpioPin& pin, std::uint32_t mode, std::uint32_t pull = GPIO_NOPULL,
                      std::uint32_t speed = GPIO_SPEED_FREQ_LOW)
{
    if (!pin.connected() || !is_ok(enable_clock(pin.port)))
        return;
    GPIO_InitTypeDef init{};
    init.Pin   = pin.pin;
    init.Mode  = mode;
    init.Pull  = pull;
    init.Speed = speed;
    HAL_GPIO_Init(pin.port, &init);
}

/// Hands the pin to its peripheral.
inline void configure(const AlternatePin& pin, std::uint32_t mode = GPIO_MODE_AF_PP, std::uint32_t pull = GPIO_NOPULL,
                      std::uint32_t speed = GPIO_SPEED_FREQ_HIGH)
{
    if (!pin.pin.connected() || !is_ok(enable_clock(pin.pin.port)))
        return;
    GPIO_InitTypeDef init{};
    init.Pin       = pin.pin.pin;
    init.Mode      = mode;
    init.Pull      = pull;
    init.Speed     = speed;
    init.Alternate = pin.alternate;
    HAL_GPIO_Init(pin.pin.port, &init);
}

/// Hands every pin of a table to its peripheral.
inline void configure(const AlternatePin* pins, std::size_t count, std::uint32_t speed)
{
    for (std::size_t index = 0; pins != nullptr && index < count; ++index)
        configure(pins[index], GPIO_MODE_AF_PP, GPIO_NOPULL, speed);
}

/// Drives the pin through BSRR, which sets or resets it in one write.
inline void write(const GpioPin& pin, bool high)
{
    if (!pin.connected())
        return;
    pin.port->BSRR = high ? pin.pin : static_cast<std::uint32_t>(pin.pin) << 16;
}

inline bool read(const GpioPin& pin)
{
    return pin.connected() && (pin.port->IDR & pin.pin) != 0;
}

}  // namespace brick::platform::stm32::h7
