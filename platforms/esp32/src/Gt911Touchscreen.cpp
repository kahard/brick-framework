#include "brick/platform/esp32/touch/Gt911Touchscreen.h"

namespace brick::platform::esp32::touch
{

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
    if (config_.reset_gpio != GPIO_NUM_NC)
    {
        gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config_.reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(2));
        gpio_set_level(config_.reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(55));
    }
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
    if (const Status status = probe_(); !is_ok(status))
    {
        // The GT911 answers at 0x5D or 0x14 depending on the INT level at reset.
        if (config_.address != 0x5D)
            return status;
        config_.address = 0x14;
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
    std::uint8_t state = 0;
    if (const Status status = read_register_(0x814E, &state, 1); !is_ok(status))
        return status;
    // Bit 7: the controller has a new sample.
    if ((state & 0x80) == 0)
        return Status::no_data;
    const auto touch_count = static_cast<std::size_t>(state & 0x07);
    if (touch_count > kMaxTouches)
        return Status::error;
    if (const Status status = write_register_(0x814E, 0); !is_ok(status))
        return status;
    const std::size_t                         bytes = touch_count * 8;
    std::array<std::uint8_t, kMaxTouches * 8> raw{};
    if (bytes != 0)
        if (const Status status = read_register_(0x814F, raw.data(), bytes); !is_ok(status))
            return status;
    std::array<bool, kMaxTouches> seen{};
    for (std::size_t index = 0; index < touch_count && count < capacity; ++index)
    {
        const auto* data = raw.data() + index * 8;
        const auto  id   = data[0] & 0x0F;
        if (id >= kMaxTouches)
            continue;
        auto point      = mapper_.map(id, static_cast<std::int16_t>(data[1] | (data[2] << 8)),
                                      static_cast<std::int16_t>(data[3] | (data[4] << 8)),
                                      static_cast<std::int16_t>(data[5] | (data[6] << 8)));
        point.state     = active_[id] ? brick::interfaces::display::TouchState::moved :
                                        brick::interfaces::display::TouchState::pressed;
        points[count++] = point;
        seen[id]        = true;
    }
    for (std::size_t id = 0; id < kMaxTouches && count < capacity; ++id)
    {
        if (active_[id] && !seen[id])
        {
            auto point      = last_points_[id];
            point.state     = brick::interfaces::display::TouchState::released;
            points[count++] = point;
        }
    }
    active_ = seen;
    for (std::size_t index = 0; index < count; ++index)
        if (points[index].state != brick::interfaces::display::TouchState::released)
            last_points_[points[index].id] = points[index];
    return Status::ok;
}

Status Gt911Touchscreen::probe_()
{
    std::uint8_t value = 0;
    return read_register_(0x8047, &value, 1);
}

Status Gt911Touchscreen::read_calibration_()
{
    std::uint8_t data[4] = {};
    if (const Status status = read_register_(0x8048, data, sizeof(data)); !is_ok(status))
        return status;
    config_.calibration.raw_x_min = config_.calibration.raw_y_min = 0;
    config_.calibration.raw_x_max                                 = static_cast<std::int16_t>(data[0] | (data[1] << 8));
    config_.calibration.raw_y_max                                 = static_cast<std::int16_t>(data[2] | (data[3] << 8));
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
