#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/platform/stm32/h7/Gpio.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

struct Stm32SpiBusConfig
{
    SPI_TypeDef* instance = nullptr;
    AlternatePin sck;
    AlternatePin miso;
    AlternatePin mosi;
    /// Divider of the SPI kernel clock (PLL1 Q, 200 MHz with the default
    /// clock): 16 gives 12.5 MHz.
    std::uint32_t baud_rate_prescaler = SPI_BAUDRATEPRESCALER_16;
};

/// An SPI master in mode 0, 8-bit, MSB first; chip selects belong to the
/// devices on the bus. The pins keep their level between transfers.
class Stm32SpiBus final
{
public:
    explicit Stm32SpiBus(Stm32SpiBusConfig config) : config_(config) {}

    Status             begin();
    SPI_HandleTypeDef& handle() { return handle_; }

private:
    Stm32SpiBusConfig config_;
    SPI_HandleTypeDef handle_{};
    bool              started_ = false;
};

}  // namespace brick::platform::stm32::h7
