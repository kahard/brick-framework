#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "brick/interfaces/display/display_device.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32 {

struct Ili9341SpiDisplayConfig {
  std::uint16_t width = 320;
  std::uint16_t height = 240;
  spi_host_device_t spi_host = SPI2_HOST;
  gpio_num_t sclk_gpio = GPIO_NUM_NC;
  gpio_num_t mosi_gpio = GPIO_NUM_NC;
  gpio_num_t miso_gpio = GPIO_NUM_NC;
  gpio_num_t cs_gpio = GPIO_NUM_NC;
  gpio_num_t dc_gpio = GPIO_NUM_NC;
  gpio_num_t backlight_gpio = GPIO_NUM_NC;
  bool backlight_active_high = true;
  int spi_mode = 0;
  std::uint32_t spi_clock_hz = 40'000'000;
  bool bgr = true;
  std::uint8_t madctl = 0x28;
};

class Ili9341SpiDisplay final
    : public brick::interfaces::display::IDisplayDevice {
 public:
  explicit Ili9341SpiDisplay(Ili9341SpiDisplayConfig config)
      : config_(config) {}

  ~Ili9341SpiDisplay() override {
    if (spi_device_ != nullptr) spi_bus_remove_device(spi_device_);
    if (spi_ready_) spi_bus_free(config_.spi_host);
  }

  bool begin() override {
    if (started_) return true;
    if (config_.sclk_gpio == GPIO_NUM_NC || config_.mosi_gpio == GPIO_NUM_NC ||
        config_.cs_gpio == GPIO_NUM_NC || config_.dc_gpio == GPIO_NUM_NC) {
      return false;
    }

    gpio_set_direction(config_.dc_gpio, GPIO_MODE_OUTPUT);
    if (config_.backlight_gpio != GPIO_NUM_NC) {
      gpio_set_direction(config_.backlight_gpio, GPIO_MODE_OUTPUT);
      gpio_set_level(config_.backlight_gpio,
                     config_.backlight_active_high ? 1 : 0);
    }
    if (!begin_spi_() || !initialize_panel_()) return false;
    if (config_.backlight_gpio != GPIO_NUM_NC) {
      gpio_set_level(config_.backlight_gpio,
                     config_.backlight_active_high ? 1 : 0);
    }
    started_ = true;
    ESP_LOGI(TAG, "ILI9341 SPI display ready %ux%u", config_.width,
             config_.height);
    return true;
  }

  brick::interfaces::display::DisplaySize size() const override {
    return {config_.width, config_.height};
  }

  brick::interfaces::display::PixelFormat pixel_format() const override {
    return brick::interfaces::display::PixelFormat::rgb565;
  }

  bool set_rotation(brick::interfaces::display::Rotation rotation) override {
    if (rotation != brick::interfaces::display::Rotation::rotate_0) {
      ESP_LOGW(TAG, "ILI9341 smoke profile supports only rotation 0");
      return false;
    }
    return send_command_(0x36, {config_.madctl});
  }

  bool draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                   std::uint16_t height, const std::uint8_t* pixels,
                   std::size_t byte_count) override {
    if (!started_ || pixels == nullptr || width == 0 || height == 0 ||
        x + width > config_.width || y + height > config_.height ||
        byte_count < static_cast<std::size_t>(width) * height * 2) {
      return false;
    }
    const std::uint16_t x2 = x + width - 1;
    const std::uint16_t y2 = y + height - 1;
    const std::uint8_t column[] = {static_cast<std::uint8_t>(x >> 8),
                                   static_cast<std::uint8_t>(x),
                                   static_cast<std::uint8_t>(x2 >> 8),
                                   static_cast<std::uint8_t>(x2)};
    const std::uint8_t row[] = {static_cast<std::uint8_t>(y >> 8),
                                static_cast<std::uint8_t>(y),
                                static_cast<std::uint8_t>(y2 >> 8),
                                static_cast<std::uint8_t>(y2)};
    return send_command_(0x2A, column, sizeof(column)) &&
           send_command_(0x2B, row, sizeof(row)) &&
           send_command_(0x2C) && send_data_(pixels,
                                              static_cast<std::size_t>(width) * height * 2);
  }

 private:
  bool begin_spi_() {
    spi_bus_config_t bus = {};
    bus.sclk_io_num = config_.sclk_gpio;
    bus.mosi_io_num = config_.mosi_gpio;
    bus.miso_io_num = config_.miso_gpio;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 4096;
    auto err = spi_bus_initialize(config_.spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
      return false;
    }
    spi_ready_ = err == ESP_OK;

    spi_device_interface_config_t device = {};
    device.clock_speed_hz = static_cast<int>(config_.spi_clock_hz);
    device.mode = config_.spi_mode;
    device.spics_io_num = config_.cs_gpio;
    device.queue_size = 1;
    err = spi_bus_add_device(config_.spi_host, &device, &spi_device_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "ILI9341 SPI device init failed: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  bool transmit_(bool data, const std::uint8_t* bytes, std::size_t length) {
    gpio_set_level(config_.dc_gpio, data ? 1 : 0);
    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    transaction.tx_buffer = bytes;
    return spi_device_polling_transmit(spi_device_, &transaction) == ESP_OK;
  }

  bool send_command_(std::uint8_t command) {
    return transmit_(false, &command, 1);
  }

  bool send_command_(std::uint8_t command,
                     std::initializer_list<std::uint8_t> args) {
    if (!send_command_(command)) return false;
    const auto* data = args.begin();
    return args.size() == 0 || transmit_(true, data, args.size());
  }

  bool send_command_(std::uint8_t command, const std::uint8_t* args,
                     std::size_t length) {
    if (!send_command_(command)) return false;
    return length == 0 || transmit_(true, args, length);
  }

  bool send_data_(const std::uint8_t* data, std::size_t length) {
    constexpr std::size_t kChunk = 4096;
    while (length != 0) {
      const auto chunk = std::min(length, kChunk);
      if (!transmit_(true, data, chunk)) return false;
      data += chunk;
      length -= chunk;
    }
    return true;
  }

  bool initialize_panel_() {
    if (!send_command_(0x01)) return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!send_command_(0x28) || !send_command_(0x3A, {0x55}) ||
        !send_command_(0x36, {config_.madctl}) ||
        !send_command_(0xC0, {0x23}) || !send_command_(0xC1, {0x10}) ||
        !send_command_(0xC5, {0x3E, 0x28}) || !send_command_(0xC7, {0x86}) ||
        !send_command_(0xB1, {0x00, 0x18}) || !send_command_(0xB6, {0x08, 0x82, 0x27}) ||
        !send_command_(0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                              0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}) ||
        !send_command_(0xE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                              0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}) ||
        !send_command_(0x11)) return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!send_command_(0x29)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
  }

  Ili9341SpiDisplayConfig config_;
  spi_device_handle_t spi_device_ = nullptr;
  bool spi_ready_ = false;
  bool started_ = false;
  static constexpr const char* TAG = "brick_ili9341_spi";
};

}  // namespace brick::platform::esp32
