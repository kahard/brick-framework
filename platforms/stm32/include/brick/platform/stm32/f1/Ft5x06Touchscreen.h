#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/ITouchscreen.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct Ft5x06TouchscreenConfig
{
    I2C_HandleTypeDef* i2c            = nullptr;
    std::uint8_t       address        = 0x38;
    std::uint16_t      width          = 480;
    std::uint16_t      height         = 272;
    std::uint16_t      raw_width      = 1280;
    std::uint16_t      raw_height     = 768;
    GPIO_TypeDef*      interrupt_port = GPIOB;
    std::uint16_t      interrupt_pin  = GPIO_PIN_1;
    GPIO_TypeDef*      reset_port     = GPIOC;
    std::uint16_t      reset_pin      = GPIO_PIN_5;
    GPIO_TypeDef*      wake_port      = GPIOB;
    std::uint16_t      wake_pin       = GPIO_PIN_0;
};

class Ft5x06Touchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    explicit Ft5x06Touchscreen(Ft5x06TouchscreenConfig config);
    bool begin() override;
    bool read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;

private:
    void write_pin_(GPIO_TypeDef* port, std::uint16_t pin, bool high) const;
    void configure_pin_(GPIO_TypeDef* port, std::uint16_t pin, std::uint32_t mode) const;

    Ft5x06TouchscreenConfig config_;
    bool                    was_pressed_ = false;
};

}  // namespace brick::platform::stm32::f1
