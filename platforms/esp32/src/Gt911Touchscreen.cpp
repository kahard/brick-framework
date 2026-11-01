#include "brick/platform/esp32/touch/Gt911Touchscreen.h"

namespace brick::platform::esp32::touch
{

namespace
{

    constexpr std::uint16_t kConfigVersionRegister = 0x8047;
    constexpr std::uint16_t kResolutionRegister    = 0x8048;
    constexpr std::uint16_t kStatusRegister        = 0x814E;
    constexpr std::uint16_t kPointsRegister        = 0x814F;
    constexpr std::uint8_t  kStatusSampleReady     = 0x80;
    constexpr std::uint8_t  kStatusTouchCountMask  = 0x0F;
    constexpr std::uint8_t  kPointIdMask           = 0x0F;
    // The GT911 answers at 0x5D or 0x14 depending on the INT level at reset.
    constexpr std::uint8_t  kDefaultAddress   = 0x5D;
    constexpr std::uint8_t  kAlternateAddress = 0x14;
    constexpr std::uint32_t kResetPulseMs     = 2;
    constexpr std::uint32_t kStartupMs        = 55;

    std::int16_t read_i16(const std::uint8_t* data)
    {
        return static_cast<std::int16_t>(data[0] | (data[1] << 8));
    }

}  // namespace

Gt911Touchscreen::Gt911Touchscreen(Gt911Config config) : config_(config)
{
}

Status Gt911Touchscreen::begin()
{
    if (started_)
        return Status::ok;
    if (config_.sda_gpio == GPIO_NUM_NC || config_.scl_gpio == GPIO_NUM_NC || config_.display_size.width == 0
        || config_.display_size.height == 0)
        return Status::invalid_argument;
    reset_controller_();
    if (const Status status = begin_i2c_(); !is_ok(status))
        return status;
    if (const Status status = probe_(); !is_ok(status))
    {
        if (config_.address != kDefaultAddress)
            return status;
        config_.address = kAlternateAddress;
        if (const Status retry = probe_(); !is_ok(retry))
            return retry;
    }
    if (config_.read_calibration_from_controller
        && (config_.calibration.raw_x_max == 0 || config_.calibration.raw_y_max == 0))
        if (const Status status = read_calibration_(); !is_ok(status))
            return status;
    mapper_  = brick::core::input::TouchMapper(config_.display_size, config_.calibration);
    started_ = true;
    return Status::ok;
}

Status Gt911Touchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (!started_)
        return Status::not_ready;
    if (points == nullptr || capacity == 0)
        return Status::invalid_argument;
    std::array<std::uint8_t, kMaxTouches * kPointBytes> raw{};
    std::size_t                                         touch_count = 0;
    if (const Status status = read_sample_(raw.data(), touch_count); !is_ok(status))
        return status;
    count = report_touches_(raw.data(), touch_count, points, capacity);
    return Status::ok;
}

void Gt911Touchscreen::reset_controller_()
{
    if (config_.reset_gpio == GPIO_NUM_NC)
        return;
    gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(config_.reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(kResetPulseMs));
    gpio_set_level(config_.reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(kStartupMs));
}

Status Gt911Touchscreen::begin_i2c_()
{
    i2c_config_t i2c_config     = {};
    i2c_config.mode             = I2C_MODE_MASTER;
    i2c_config.sda_io_num       = config_.sda_gpio;
    i2c_config.scl_io_num       = config_.scl_gpio;
    i2c_config.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    i2c_config.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    i2c_config.master.clk_speed = config_.clock_hz;
    if (const esp_err_t err = i2c_param_config(config_.port, &i2c_config); err != ESP_OK)
        return to_status(err);
    // ESP_ERR_INVALID_STATE: the driver is already installed for this port.
    const esp_err_t install_result = i2c_driver_install(config_.port, I2C_MODE_MASTER, 0, 0, 0);
    if (install_result != ESP_OK && install_result != ESP_ERR_INVALID_STATE)
        return to_status(install_result);
    return Status::ok;
}

Status Gt911Touchscreen::read_sample_(std::uint8_t* raw, std::size_t& touch_count)
{
    std::uint8_t state = 0;
    if (const Status status = read_register_(kStatusRegister, &state, 1); !is_ok(status))
        return status;
    if ((state & kStatusSampleReady) == 0)
        return Status::no_data;
    touch_count   = state & kStatusTouchCountMask;
    Status status = touch_count > kMaxTouches ? Status::error : Status::ok;
    if (is_ok(status) && touch_count != 0)
        status = read_register_(kPointsRegister, raw, touch_count * kPointBytes);
    // Clearing the status after the points were read lets the controller
    // write the next sample; a bad sample is cleared too, or it would stay.
    if (const Status cleared = write_register_(kStatusRegister, 0); is_ok(status))
        status = cleared;
    return status;
}

std::size_t Gt911Touchscreen::report_touches_(const std::uint8_t* raw, std::size_t touch_count,
                                              brick::interfaces::display::TouchPoint* points, std::size_t capacity)
{
    using brick::interfaces::display::TouchState;
    std::size_t                   count = 0;
    std::array<bool, kMaxTouches> seen{};
    for (std::size_t index = 0; index < touch_count && count < capacity; ++index)
    {
        const std::uint8_t* data = raw + index * kPointBytes;
        const auto          id   = data[0] & kPointIdMask;
        if (id >= kMaxTouches)
            continue;
        auto point      = mapper_.map(id, read_i16(data + 1), read_i16(data + 3), read_i16(data + 5));
        point.state     = active_[id] ? TouchState::moved : TouchState::pressed;
        points[count++] = point;
        seen[id]        = true;
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

Status Gt911Touchscreen::probe_()
{
    std::uint8_t value = 0;
    return read_register_(kConfigVersionRegister, &value, 1);
}

Status Gt911Touchscreen::read_calibration_()
{
    std::uint8_t data[4] = {};
    if (const Status status = read_register_(kResolutionRegister, data, sizeof(data)); !is_ok(status))
        return status;
    config_.calibration.raw_x_min = config_.calibration.raw_y_min = 0;
    config_.calibration.raw_x_max                                 = read_i16(data);
    config_.calibration.raw_y_max                                 = read_i16(data + 2);
    return config_.calibration.raw_x_max > 0 && config_.calibration.raw_y_max > 0 ? Status::ok : Status::error;
}

Status Gt911Touchscreen::write_register_(std::uint16_t reg, std::uint8_t value)
{
    const std::uint8_t data[] = { static_cast<std::uint8_t>(reg >> 8), static_cast<std::uint8_t>(reg), value };
    return to_status(i2c_master_write_to_device(config_.port, config_.address, data, sizeof(data), kTimeout));
}

Status Gt911Touchscreen::read_register_(std::uint16_t reg, std::uint8_t* data, std::size_t length)
{
    const std::uint8_t address[] = { static_cast<std::uint8_t>(reg >> 8), static_cast<std::uint8_t>(reg) };
    return to_status(
        i2c_master_write_read_device(config_.port, config_.address, address, sizeof(address), data, length, kTimeout));
}

}  // namespace brick::platform::esp32::touch
