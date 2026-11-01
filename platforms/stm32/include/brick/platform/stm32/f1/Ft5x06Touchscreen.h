#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/ITouchscreen.h"
#include "brick/platform/stm32/f1/Gpio.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct Ft5x06TouchscreenConfig
{
    I2C_HandleTypeDef* i2c     = nullptr;
    std::uint8_t       address = 0x38;  ///< 7-bit
    /// Display size and the controller's coordinate range it maps onto.
    std::uint16_t width      = 0;
    std::uint16_t height     = 0;
    std::uint16_t raw_width  = 0;
    std::uint16_t raw_height = 0;
    GpioPin       interrupt;
    GpioPin       reset;
    GpioPin       wake;
};

/// FT5x06 capacitive touch controller, first touch point only, on an I2C
/// bus the board starts (Stm32I2cBus).
class Ft5x06Touchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    explicit Ft5x06Touchscreen(Ft5x06TouchscreenConfig config) : config_(config) {}

    Status begin() override;
    Status read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;

private:
    Ft5x06TouchscreenConfig config_;
    bool                    started_     = false;
    bool                    was_pressed_ = false;
};

}  // namespace brick::platform::stm32::f1
