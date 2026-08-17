#pragma once

#include <Arduino.h>

#include <cstdint>

#include "brick/interfaces/input/button.hpp"

namespace brick::platform::esp8266 {

struct GpioButtonConfig {
  std::uint8_t gpio = 4;
  std::uint8_t pin_mode = INPUT;
  std::uint8_t active_level = HIGH;
};

class GpioButton final : public brick::interfaces::input::IButton {
 public:
  explicit GpioButton(GpioButtonConfig config) : config_(config) {}

  bool begin() override {
    pinMode(config_.gpio, config_.pin_mode);
    initialized_ = true;
    return true;
  }

  bool is_pressed() const override {
    return initialized_ && digitalRead(config_.gpio) == config_.active_level;
  }

 private:
  GpioButtonConfig config_;
  bool initialized_ = false;
};

}  // namespace brick::platform::esp8266
