#include "brick/platform/stm32/f1/Ft5x06Touchscreen.h"

#include <algorithm>

#include "brick/platform/stm32/f1/HalStatus.h"

namespace brick::platform::stm32::f1
{

namespace
{

    // Register 0xF9 starts a block whose bytes 3 and 5..8 hold the touch
    // count and the first point.
    constexpr std::uint16_t kTouchRegister   = 0xF9;
    constexpr std::size_t   kTouchBlockBytes = 9;
    constexpr std::uint32_t kI2cTimeoutMs    = 100;
    constexpr std::uint32_t kWakePulseMs     = 50;
    constexpr std::uint32_t kResetPulseMs    = 50;
    constexpr std::uint32_t kStartupMs       = 100;

}  // namespace

Status Ft5x06Touchscreen::begin()
{
    if (started_)
        return Status::ok;
    if (config_.i2c == nullptr || config_.width == 0 || config_.height == 0 || config_.raw_width == 0
        || config_.raw_height == 0)
        return Status::invalid_argument;
    configure(config_.interrupt, GPIO_MODE_INPUT, GPIO_PULLUP);
    configure(config_.reset, GPIO_MODE_OUTPUT_PP);
    configure(config_.wake, GPIO_MODE_OUTPUT_PP);
    write(config_.wake, false);
    HAL_Delay(kWakePulseMs);
    write(config_.wake, true);
    HAL_Delay(kWakePulseMs);
    write(config_.reset, false);
    HAL_Delay(kResetPulseMs);
    write(config_.reset, true);
    HAL_Delay(kStartupMs);
    started_ = true;
    return Status::ok;
}

Status Ft5x06Touchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count)
{
    using brick::interfaces::display::TouchState;
    count = 0;
    if (!started_)
        return Status::not_ready;
    if (points == nullptr || capacity == 0)
        return Status::invalid_argument;
    std::uint8_t data[kTouchBlockBytes]{};
    if (const HAL_StatusTypeDef result =
            HAL_I2C_Mem_Read(config_.i2c, static_cast<std::uint16_t>(config_.address) << 1, kTouchRegister,
                             I2C_MEMADD_SIZE_8BIT, data, sizeof(data), kI2cTimeoutMs);
        result != HAL_OK)
        return to_status(result);
    if (std::min<std::uint8_t>(data[3], 1) == 0)
    {
        if (!was_pressed_)
            return Status::no_data;
        points[0].state = TouchState::released;
        count           = 1;
        was_pressed_    = false;
        return Status::ok;
    }
    const auto raw_x = static_cast<std::uint16_t>(((data[5] & 0x0F) << 8) | data[6]);
    const auto raw_y = static_cast<std::uint16_t>(((data[7] & 0x0F) << 8) | data[8]);
    auto&      point = points[0];
    point.id         = 0;
    point.raw_x      = static_cast<std::int16_t>(raw_x);
    point.raw_y      = static_cast<std::int16_t>(raw_y);
    point.x          = static_cast<std::int16_t>(raw_x * config_.width / config_.raw_width);
    point.y          = static_cast<std::int16_t>(raw_y * config_.height / config_.raw_height);
    point.state      = was_pressed_ ? TouchState::moved : TouchState::pressed;
    was_pressed_     = true;
    count            = 1;
    return Status::ok;
}

}  // namespace brick::platform::stm32::f1
