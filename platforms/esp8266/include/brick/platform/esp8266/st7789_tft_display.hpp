#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/display_device.hpp"

namespace brick::platform::esp8266 {

struct St7789TftDisplayConfig {
  std::uint16_t width = 240;
  std::uint16_t height = 240;
  std::uint8_t rotation = 0;
  std::uint8_t backlight_gpio = 5;
  std::uint8_t backlight_on_level = LOW;
  bool enable_backlight = true;
  bool swap_bytes = true;
};

class St7789TftDisplay final
    : public brick::interfaces::display::IDisplayDevice {
 public:
  explicit St7789TftDisplay(St7789TftDisplayConfig config)
      : config_(config) {}

  bool begin() override {
    if (initialized_) return true;
    if (config_.width == 0 || config_.height == 0 || config_.rotation > 3)
      return false;

    if (config_.enable_backlight) {
      pinMode(config_.backlight_gpio, OUTPUT);
      digitalWrite(config_.backlight_gpio, config_.backlight_on_level);
    }

    tft_.init();
    tft_.setSwapBytes(config_.swap_bytes);
    tft_.setRotation(config_.rotation);
    initialized_ = true;
    return true;
  }

  brick::interfaces::display::DisplaySize size() const override {
    if (config_.rotation % 2 == 0)
      return {config_.width, config_.height};
    return {config_.height, config_.width};
  }

  brick::interfaces::display::PixelFormat pixel_format() const override {
    return brick::interfaces::display::PixelFormat::rgb565;
  }

  bool set_rotation(brick::interfaces::display::Rotation rotation) override {
    const auto value = static_cast<std::uint8_t>(rotation);
    if (value > 3) return false;
    config_.rotation = value;
    if (initialized_) tft_.setRotation(config_.rotation);
    return true;
  }

  bool draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                   std::uint16_t height, const std::uint8_t* pixels,
                   std::size_t byte_count) override {
    const auto display_size = size();
    const auto required = static_cast<std::size_t>(width) * height * 2;
    if (!initialized_ || pixels == nullptr || width == 0 || height == 0 ||
        x + width > display_size.width || y + height > display_size.height ||
        byte_count < required)
      return false;

    tft_.pushImage(x, y, width, height,
                   const_cast<std::uint16_t*>(
                       reinterpret_cast<const std::uint16_t*>(pixels)));
    return true;
  }

 private:
  St7789TftDisplayConfig config_;
  TFT_eSPI tft_;
  bool initialized_ = false;
};

}  // namespace brick::platform::esp8266
