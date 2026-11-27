#include "brick/platform/stm32/h7/CapacitiveTouchscreen.h"

#include <cstring>

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

namespace
{
    constexpr std::uint32_t kI2cTimeoutMs = 20;
    constexpr std::uint32_t kProbeTrials  = 2;
    // After power-on the controller needs a while before it answers on I2C
    // (a reset of the processor alone finds it ready at once).
    constexpr std::uint32_t kStartupTimeoutMs = 500;
    constexpr std::uint32_t kProbeRetryMs     = 20;
    constexpr std::uint16_t kRegister8Bit     = I2C_MEMADD_SIZE_8BIT;
    constexpr std::uint16_t kRegister16Bit    = I2C_MEMADD_SIZE_16BIT;

    // FT6x36: register 0x02 holds the number of touches, then six bytes per
    // point: event and X high, X low, touch id and Y high, Y low, weight, area.
    constexpr std::uint8_t kFtAddress        = 0x38;
    constexpr std::uint8_t kFtChipIdRegister = 0xA3;
    constexpr std::uint8_t kFtStatusRegister = 0x02;
    constexpr std::size_t  kFtMaxTouches     = 2;
    constexpr std::size_t  kFtPointBytes     = 6;
    constexpr std::uint8_t kFtEventLiftUp    = 1;
    constexpr std::uint8_t kFtEventNone      = 3;
    constexpr std::uint8_t kFtChipIds[]      = { 0x06, 0x36, 0x64, 0x54 };  // FT6206, FT6236, FT6336, FT6354

    // GT911: status (bit 7 = new sample, low bits = touches), then eight
    // bytes per point: id, X, Y, size (little endian), reserved.
    constexpr std::uint8_t  kGtAddress            = 0x5D;
    constexpr std::uint8_t  kGtAlternateAddress   = 0x14;
    constexpr std::uint16_t kGtProductIdRegister  = 0x8140;
    constexpr std::uint16_t kGtResolutionRegister = 0x8048;
    constexpr std::uint16_t kGtStatusRegister     = 0x814E;
    constexpr std::uint16_t kGtPointsRegister     = 0x814F;
    constexpr std::uint8_t  kGtSampleReady        = 0x80;
    constexpr std::uint8_t  kGtTouchCountMask     = 0x0F;
    constexpr std::size_t   kGtPointBytes         = 8;

    std::int16_t little_endian(const std::uint8_t* data)
    {
        return static_cast<std::int16_t>(data[0] | (data[1] << 8));
    }
}  // namespace

Status CapacitiveTouchscreen::begin()
{
    if (controller_ != Controller::none)
        return Status::ok;
    if (config_.i2c == nullptr || config_.display_size.width == 0 || config_.display_size.height == 0)
        return Status::invalid_argument;
    const std::uint32_t start = HAL_GetTick();
    Controller          found = find_controller_();
    while (found == Controller::none && HAL_GetTick() - start < kStartupTimeoutMs)
    {
        HAL_Delay(kProbeRetryMs);
        found = find_controller_();
    }
    if (found == Controller::none)
        return Status::not_found;

    brick::core::input::TouchCalibration calibration = config_.calibration;
    if (calibration.raw_x_max == 0 || calibration.raw_y_max == 0)
    {
        if (found == Controller::ft6x36)
        {
            calibration.raw_x_max = static_cast<std::int16_t>(config_.display_size.width);
            calibration.raw_y_max = static_cast<std::int16_t>(config_.display_size.height);
        }
        else
        {
            std::uint8_t data[4]{};
            if (const Status status = read_registers_(kGtResolutionRegister, kRegister16Bit, data, sizeof(data));
                !is_ok(status))
                return status;
            calibration.raw_x_max = little_endian(data);
            calibration.raw_y_max = little_endian(data + 2);
        }
    }
    if (calibration.raw_x_max <= 0 || calibration.raw_y_max <= 0)
        return Status::error;
    mapper_     = brick::core::input::TouchMapper(config_.display_size, calibration);
    controller_ = found;
    return Status::ok;
}

/// One look at the bus for either controller.
CapacitiveTouchscreen::Controller CapacitiveTouchscreen::find_controller_()
{
    if (is_ok(probe_ft6x36_()))
        return Controller::ft6x36;
    if (is_ok(probe_gt911_(kGtAddress)) || is_ok(probe_gt911_(kGtAlternateAddress)))
        return Controller::gt911;
    return Controller::none;
}

Status CapacitiveTouchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity,
                                   std::size_t& count)
{
    count = 0;
    if (controller_ == Controller::none)
        return Status::not_ready;
    if (points == nullptr || capacity == 0)
        return Status::invalid_argument;
    std::size_t  touch_count = 0;
    const Status status      = controller_ == Controller::ft6x36 ? read_ft6x36_(touch_count) : read_gt911_(touch_count);
    if (!is_ok(status))
        return status;
    count = report_(touch_count, points, capacity);
    return Status::ok;
}

Status CapacitiveTouchscreen::probe_ft6x36_()
{
    if (HAL_I2C_IsDeviceReady(config_.i2c, static_cast<std::uint16_t>(kFtAddress << 1), kProbeTrials, kI2cTimeoutMs)
        != HAL_OK)
        return Status::not_found;
    address_          = kFtAddress;
    std::uint8_t chip = 0;
    if (const Status status = read_registers_(kFtChipIdRegister, kRegister8Bit, &chip, 1); !is_ok(status))
        return status;
    for (const std::uint8_t id : kFtChipIds)
        if (chip == id)
            return Status::ok;
    return Status::not_found;
}

Status CapacitiveTouchscreen::probe_gt911_(std::uint8_t address)
{
    if (HAL_I2C_IsDeviceReady(config_.i2c, static_cast<std::uint16_t>(address << 1), kProbeTrials, kI2cTimeoutMs)
        != HAL_OK)
        return Status::not_found;
    address_ = address;
    std::uint8_t product[4]{};
    if (const Status status = read_registers_(kGtProductIdRegister, kRegister16Bit, product, sizeof(product));
        !is_ok(status))
        return status;
    return std::memcmp(product, "911", 3) == 0 ? Status::ok : Status::not_found;
}

/// The FT6x36 always reports its present state; Status::no_data when no
/// finger is on it and none was before.
Status CapacitiveTouchscreen::read_ft6x36_(std::size_t& touch_count)
{
    std::uint8_t data[1 + kFtMaxTouches * kFtPointBytes]{};
    if (const Status status = read_registers_(kFtStatusRegister, kRegister8Bit, data, sizeof(data)); !is_ok(status))
        return status;
    const std::size_t reported = data[0] & 0x0F;
    touch_count                = 0;
    for (std::size_t index = 0; index < reported && index < kFtMaxTouches; ++index)
    {
        const std::uint8_t* point = data + 1 + index * kFtPointBytes;
        const std::uint8_t  event = point[0] >> 6;
        if (event == kFtEventLiftUp || event == kFtEventNone)
            continue;
        RawTouch& touch = touches_[touch_count++];
        touch.id        = static_cast<std::uint8_t>(point[2] >> 4);
        touch.x         = static_cast<std::int16_t>(((point[0] & 0x0F) << 8) | point[1]);
        touch.y         = static_cast<std::int16_t>(((point[2] & 0x0F) << 8) | point[3]);
        touch.pressure  = point[4];
    }
    bool any_active = false;
    for (const bool active : active_)
        any_active = any_active || active;
    return touch_count == 0 && !any_active ? Status::no_data : Status::ok;
}

/// The GT911 flags a new sample; it is acknowledged by clearing the status.
Status CapacitiveTouchscreen::read_gt911_(std::size_t& touch_count)
{
    std::uint8_t state = 0;
    if (const Status status = read_registers_(kGtStatusRegister, kRegister16Bit, &state, 1); !is_ok(status))
        return status;
    if ((state & kGtSampleReady) == 0)
        return Status::no_data;
    touch_count = state & kGtTouchCountMask;
    std::uint8_t data[kMaxTouches * kGtPointBytes]{};
    Status       status = touch_count > kMaxTouches ? Status::error : Status::ok;
    if (is_ok(status) && touch_count != 0)
        status = read_registers_(kGtPointsRegister, kRegister16Bit, data,
                                 static_cast<std::uint16_t>(touch_count * kGtPointBytes));
    if (const Status cleared = write_register_(kGtStatusRegister, kRegister16Bit, 0); is_ok(status))
        status = cleared;
    if (!is_ok(status))
    {
        touch_count = 0;
        return status;
    }
    for (std::size_t index = 0; index < touch_count; ++index)
    {
        const std::uint8_t* point = data + index * kGtPointBytes;
        RawTouch&           touch = touches_[index];
        touch.id                  = point[0] & 0x0F;
        touch.x                   = little_endian(point + 1);
        touch.y                   = little_endian(point + 3);
        touch.pressure            = little_endian(point + 5);
    }
    return Status::ok;
}

/// Turns the fingers of this sample into points: new ids pressed, known ids
/// moved, ids gone since the last sample released (with their last position).
std::size_t CapacitiveTouchscreen::report_(std::size_t touch_count, brick::interfaces::display::TouchPoint* points,
                                           std::size_t capacity)
{
    using brick::interfaces::display::TouchState;
    std::size_t                   count = 0;
    std::array<bool, kMaxTouches> seen{};
    for (std::size_t index = 0; index < touch_count && count < capacity; ++index)
    {
        const RawTouch& touch = touches_[index];
        if (touch.id >= kMaxTouches)
            continue;
        auto point      = mapper_.map(touch.id, touch.x, touch.y, touch.pressure);
        point.state     = active_[touch.id] ? TouchState::moved : TouchState::pressed;
        points[count++] = point;
        seen[touch.id]  = true;
    }
    for (std::size_t id = 0; id < kMaxTouches && count < capacity; ++id)
    {
        if (active_[id] && !seen[id])
        {
            points[count]         = last_points_[id];
            points[count++].state = TouchState::released;
        }
    }
    active_ = seen;
    for (std::size_t index = 0; index < count; ++index)
        if (points[index].state != TouchState::released)
            last_points_[points[index].id] = points[index];
    return count;
}

Status CapacitiveTouchscreen::read_registers_(std::uint16_t reg, std::uint16_t reg_size, std::uint8_t* data,
                                              std::uint16_t length)
{
    return to_status(HAL_I2C_Mem_Read(config_.i2c, static_cast<std::uint16_t>(address_ << 1), reg, reg_size, data,
                                      length, kI2cTimeoutMs));
}

Status CapacitiveTouchscreen::write_register_(std::uint16_t reg, std::uint16_t reg_size, std::uint8_t value)
{
    return to_status(HAL_I2C_Mem_Write(config_.i2c, static_cast<std::uint16_t>(address_ << 1), reg, reg_size, &value, 1,
                                       kI2cTimeoutMs));
}

}  // namespace brick::platform::stm32::h7
