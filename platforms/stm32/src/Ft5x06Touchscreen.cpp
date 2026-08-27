#include "brick/platform/stm32/f1/Ft5x06Touchscreen.h"

#include <algorithm>

namespace brick::platform::stm32::f1
{

Ft5x06Touchscreen::Ft5x06Touchscreen(Ft5x06TouchscreenConfig config) : config_(config)
{
}

bool Ft5x06Touchscreen::begin()
{
    if (config_.i2c == nullptr)
        return false;
    configure_pin_(config_.interrupt_port, config_.interrupt_pin, GPIO_MODE_INPUT);
    configure_pin_(config_.reset_port, config_.reset_pin, GPIO_MODE_OUTPUT_PP);
    configure_pin_(config_.wake_port, config_.wake_pin, GPIO_MODE_OUTPUT_PP);
    write_pin_(config_.wake_port, config_.wake_pin, false);
    HAL_Delay(50);
    write_pin_(config_.wake_port, config_.wake_pin, true);
    HAL_Delay(50);
    write_pin_(config_.reset_port, config_.reset_pin, false);
    HAL_Delay(50);
    write_pin_(config_.reset_port, config_.reset_pin, true);
    HAL_Delay(100);
    return true;
}

bool Ft5x06Touchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (points == nullptr || capacity == 0 || config_.i2c == nullptr)
        return false;
    std::uint8_t data[9]{};
    if (HAL_I2C_Mem_Read(config_.i2c, static_cast<std::uint16_t>(config_.address) << 1, 0xF9, I2C_MEMADD_SIZE_8BIT, data, sizeof(data), 100) != HAL_OK)
        return false;
    const auto touches = std::min<std::uint8_t>(data[3], 1);
    if (touches == 0)
    {
        if (was_pressed_)
        {
            points[0].state = brick::interfaces::display::TouchState::released;
            count           = 1;
        }
        was_pressed_ = false;
        return true;
    }
    const auto raw_x = static_cast<std::uint16_t>(((data[5] & 0x0F) << 8) | data[6]);
    const auto raw_y = static_cast<std::uint16_t>(((data[7] & 0x0F) << 8) | data[8]);
    auto&      point = points[0];
    point.id         = 0;
    point.raw_x      = raw_x;
    point.raw_y      = raw_y;
    point.x          = static_cast<std::int16_t>(raw_x * config_.width / config_.raw_width);
    point.y          = static_cast<std::int16_t>(raw_y * config_.height / config_.raw_height);
    point.state      = was_pressed_ ? brick::interfaces::display::TouchState::moved : brick::interfaces::display::TouchState::pressed;
    was_pressed_     = true;
    count            = 1;
    return true;
}

void Ft5x06Touchscreen::write_pin_(GPIO_TypeDef* port, std::uint16_t pin, bool high) const
{
    port->BSRR = high ? pin : static_cast<std::uint32_t>(pin) << 16;
}
void Ft5x06Touchscreen::configure_pin_(GPIO_TypeDef* port, std::uint16_t pin, std::uint32_t mode) const
{
    GPIO_InitTypeDef init{};
    init.Pin   = pin;
    init.Mode  = mode;
    init.Pull  = mode == GPIO_MODE_INPUT ? GPIO_PULLUP : GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &init);
}

}  // namespace brick::platform::stm32::f1
