#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/platform/stm32/h7/Gpio.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

struct Stm32I2cBusConfig
{
    I2C_TypeDef* instance = nullptr;
    AlternatePin scl;
    AlternatePin sda;
    /// I2C_TIMINGR value for the bus speed at the I2C kernel clock (APB1 by
    /// default). The default is about 100 kHz from 100 MHz: PRESC 4 (50 ns),
    /// SCLL 5.0 us, SCLH 4.7 us, SDADEL 100 ns, SCLDEL 250 ns.
    std::uint32_t timing = 0x40425D63;
};

/// An I2C master. Drivers on the bus take handle().
class Stm32I2cBus final
{
public:
    explicit Stm32I2cBus(Stm32I2cBusConfig config) : config_(config) {}

    Status             begin();
    I2C_HandleTypeDef& handle() { return handle_; }

private:
    Stm32I2cBusConfig config_;
    I2C_HandleTypeDef handle_{};
    bool              started_ = false;
};

}  // namespace brick::platform::stm32::h7
