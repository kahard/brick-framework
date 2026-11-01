#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/platform/stm32/f1/Gpio.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct Stm32I2cBusConfig
{
    I2C_TypeDef*  instance = nullptr;
    GpioPin       scl;
    GpioPin       sda;
    std::uint32_t clock_hz = 100000;
};

/// An I2C master. Drivers on the bus take handle(); a pin remap (AFIO) is
/// set up by the board before begin().
class Stm32I2cBus final
{
public:
    explicit Stm32I2cBus(Stm32I2cBusConfig config) : config_(config) {}

    Status             begin();
    I2C_HandleTypeDef& handle() { return handle_; }

private:
    Stm32I2cBusConfig config_;
    I2C_HandleTypeDef handle_{};
};

struct Stm32SpiBusConfig
{
    SPI_TypeDef*  instance = nullptr;
    GpioPin       sck;
    GpioPin       miso;
    GpioPin       mosi;
    std::uint32_t baud_rate_prescaler = SPI_BAUDRATEPRESCALER_16;
};

/// An SPI master in mode 0, 8-bit, MSB first; chip selects belong to the
/// devices on the bus.
class Stm32SpiBus final
{
public:
    explicit Stm32SpiBus(Stm32SpiBusConfig config) : config_(config) {}

    Status             begin();
    SPI_HandleTypeDef& handle() { return handle_; }

private:
    Stm32SpiBusConfig config_;
    SPI_HandleTypeDef handle_{};
};

}  // namespace brick::platform::stm32::f1
