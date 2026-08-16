#pragma once

#include "brick/platform/esp32/s3/st7701s_rgb_display.hpp"
#include "brick/platform/esp32/touch/gt911_touchscreen.hpp"

namespace brick::platform::esp32::s3::profiles {

inline St7701sRgbPanelConfig st7701s_480x480() {
  // 4-inch 480x480 RGB panel used by the ESP32-S3 test project.
  static constexpr std::uint8_t sequence[] = {
      0x01, 0xFF,              // reset delay
      0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x10,
      0xCD, 0x01, 0x00,
  };
  St7701sRgbPanelConfig config{};
  config.width = 480;
  config.height = 480;
  config.pixel_clock_hz = 12'000'000;
  config.spi_sclk_gpio = GPIO_NUM_48;
  config.spi_mosi_gpio = GPIO_NUM_47;
  config.spi_miso_gpio = GPIO_NUM_41;
  config.spi_cs_gpio = GPIO_NUM_39;
  config.hsync_gpio = GPIO_NUM_16;
  config.vsync_gpio = GPIO_NUM_17;
  config.de_gpio = GPIO_NUM_18;
  config.pclk_gpio = GPIO_NUM_21;
  config.data_gpios = {11, 12, 13, 14, 0, 8, 20, 3,
                       46, 9, 10, 4, 5, 6, 7, 15};
  config.init_sequence = sequence;
  config.init_sequence_size = sizeof(sequence);
  return config;
}

inline brick::platform::esp32::touch::Gt911Config st7701s_gt911() {
  brick::platform::esp32::touch::Gt911Config config{};
  config.port = I2C_NUM_0;
  config.sda_gpio = GPIO_NUM_19;
  config.scl_gpio = GPIO_NUM_45;
  config.clock_hz = 100'000;
  config.display_size = {480, 480};
  return config;
}

}  // namespace brick::platform::esp32::s3::profiles
