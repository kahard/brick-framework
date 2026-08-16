#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/core/input/touch_mapper.hpp"
#include "brick/interfaces/display/touchscreen.hpp"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace brick::platform::esp32::touch {

struct Gt911Config {
  i2c_port_t port = I2C_NUM_0;
  gpio_num_t sda_gpio = GPIO_NUM_NC;
  gpio_num_t scl_gpio = GPIO_NUM_NC;
  gpio_num_t reset_gpio = GPIO_NUM_NC;
  gpio_num_t interrupt_gpio = GPIO_NUM_NC;
  std::uint8_t address = 0x5D;
  std::uint32_t clock_hz = 400000;
  brick::interfaces::display::DisplaySize display_size{};
  brick::core::input::TouchCalibration calibration{};
  bool read_calibration_from_controller = true;
};

class Gt911Touchscreen final : public brick::interfaces::display::ITouchscreen {
 public:
  explicit Gt911Touchscreen(Gt911Config config) : config_(config) {}

  bool begin() override {
    if (started_) return true;
    if (config_.sda_gpio == GPIO_NUM_NC || config_.scl_gpio == GPIO_NUM_NC ||
        config_.display_size.width == 0 || config_.display_size.height == 0) {
      return false;
    }

    if (config_.reset_gpio != GPIO_NUM_NC) {
      gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
      gpio_set_level(config_.reset_gpio, 0);
      vTaskDelay(pdMS_TO_TICKS(2));
      gpio_set_level(config_.reset_gpio, 1);
      vTaskDelay(pdMS_TO_TICKS(55));
    }

    i2c_config_t i2c_config = {};
    i2c_config.mode = I2C_MODE_MASTER;
    i2c_config.sda_io_num = config_.sda_gpio;
    i2c_config.scl_io_num = config_.scl_gpio;
    i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_config.master.clk_speed = config_.clock_hz;
    if (i2c_param_config(config_.port, &i2c_config) != ESP_OK) return false;
    const auto install_result = i2c_driver_install(config_.port, I2C_MODE_MASTER,
                                                   0, 0, 0);
    if (install_result != ESP_OK && install_result != ESP_ERR_INVALID_STATE) {
      return false;
    }

    if (!probe_()) {
      if (config_.address == 0x5D) {
        config_.address = 0x14;
        if (!probe_()) return false;
      } else {
        return false;
      }
    }

    if (config_.read_calibration_from_controller &&
        (config_.calibration.raw_x_max == 0 ||
         config_.calibration.raw_y_max == 0)) {
      if (!read_calibration_()) return false;
    }
    mapper_ = brick::core::input::TouchMapper(config_.display_size,
                                               config_.calibration);
    started_ = true;
    return true;
  }

  bool read(brick::interfaces::display::TouchPoint* points,
            std::size_t capacity, std::size_t& count) override {
    count = 0;
    if (!started_ || points == nullptr || capacity == 0) return false;

    std::uint8_t state = 0;
    if (!read_register_(0x814E, &state, 1)) return false;
    if ((state & 0x80) == 0) return true;

    const auto touch_count = static_cast<std::size_t>(state & 0x07);
    if (touch_count > kMaxTouches) return false;
    const std::size_t bytes = touch_count * 8;
    std::array<std::uint8_t, kMaxTouches * 8> raw{};
    if (!write_register_(0x814E, 0)) return false;
    if (bytes != 0 && !read_register_(0x814F, raw.data(), bytes)) return false;

    std::array<bool, kMaxTouches> seen{};
    for (std::size_t index = 0; index < touch_count && count < capacity; ++index) {
      const auto* data = raw.data() + index * 8;
      const auto id = data[0] & 0x0F;
      if (id >= kMaxTouches) continue;
      const auto raw_x = static_cast<std::int16_t>(data[1] | (data[2] << 8));
      const auto raw_y = static_cast<std::int16_t>(data[3] | (data[4] << 8));
      const auto pressure = static_cast<std::int16_t>(data[5] | (data[6] << 8));
      auto point = mapper_.map(id, raw_x, raw_y, pressure);
      point.state = active_[id] ? brick::interfaces::display::TouchState::moved
                                : brick::interfaces::display::TouchState::pressed;
      points[count++] = point;
      seen[id] = true;
    }

    for (std::size_t id = 0; id < kMaxTouches && count < capacity; ++id) {
      if (active_[id] && !seen[id]) {
        auto point = last_points_[id];
        point.state = brick::interfaces::display::TouchState::released;
        points[count++] = point;
      }
    }
    active_ = seen;
    for (std::size_t id = 0; id < count; ++id) {
      if (points[id].state != brick::interfaces::display::TouchState::released)
        last_points_[points[id].id] = points[id];
    }
    return true;
  }

 private:
  static constexpr std::size_t kMaxTouches = 5;
  static constexpr TickType_t kTimeout = pdMS_TO_TICKS(100);

  bool probe_() {
    std::uint8_t value = 0;
    return read_register_(0x8047, &value, 1);
  }

  bool read_calibration_() {
    std::uint8_t data[4] = {};
    if (!read_register_(0x8048, data, sizeof(data))) return false;
    config_.calibration.raw_x_min = 0;
    config_.calibration.raw_y_min = 0;
    config_.calibration.raw_x_max = static_cast<std::int16_t>(data[0] | (data[1] << 8));
    config_.calibration.raw_y_max = static_cast<std::int16_t>(data[2] | (data[3] << 8));
    return config_.calibration.raw_x_max > 0 && config_.calibration.raw_y_max > 0;
  }

  bool write_register_(std::uint16_t reg, std::uint8_t value) {
    const std::uint8_t data[] = {static_cast<std::uint8_t>(reg >> 8),
                                 static_cast<std::uint8_t>(reg), value};
    return i2c_master_write_to_device(config_.port, config_.address, data,
                                      sizeof(data), kTimeout) == ESP_OK;
  }

  bool read_register_(std::uint16_t reg, std::uint8_t* data,
                      std::size_t length) {
    const std::uint8_t address[] = {static_cast<std::uint8_t>(reg >> 8),
                                    static_cast<std::uint8_t>(reg)};
    return i2c_master_write_read_device(config_.port, config_.address, address,
                                        sizeof(address), data, length,
                                        kTimeout) == ESP_OK;
  }

  Gt911Config config_;
  brick::core::input::TouchMapper mapper_{{}, {}};
  std::array<bool, kMaxTouches> active_{};
  std::array<brick::interfaces::display::TouchPoint, kMaxTouches> last_points_{};
  bool started_ = false;
};

}  // namespace brick::platform::esp32::touch
