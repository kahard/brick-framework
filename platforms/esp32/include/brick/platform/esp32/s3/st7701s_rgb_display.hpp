#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/display_device.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32::s3 {

struct St7701sRgbPanelConfig {
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint32_t pixel_clock_hz = 12'000'000;
  std::uint16_t hsync_pulse_width = 10;
  std::uint16_t hsync_back_porch = 10;
  std::uint16_t hsync_front_porch = 20;
  std::uint16_t vsync_pulse_width = 10;
  std::uint16_t vsync_back_porch = 10;
  std::uint16_t vsync_front_porch = 20;
  bool pclk_active_neg = false;

  spi_host_device_t spi_host = SPI2_HOST;
  gpio_num_t spi_sclk_gpio = GPIO_NUM_NC;
  gpio_num_t spi_mosi_gpio = GPIO_NUM_NC;
  gpio_num_t spi_miso_gpio = GPIO_NUM_NC;
  gpio_num_t spi_cs_gpio = GPIO_NUM_NC;
  int spi_mode = 3;
  std::uint32_t spi_clock_hz = 2'000'000;

  gpio_num_t hsync_gpio = GPIO_NUM_NC;
  gpio_num_t vsync_gpio = GPIO_NUM_NC;
  gpio_num_t de_gpio = GPIO_NUM_NC;
  gpio_num_t pclk_gpio = GPIO_NUM_NC;
  std::array<int, 16> data_gpios{};
  gpio_num_t reset_gpio = GPIO_NUM_NC;

  // Sequence format: command, argument_count, arguments...
  // A command with argument_count 0xFF is a delay in milliseconds.
  const std::uint8_t* init_sequence = nullptr;
  std::size_t init_sequence_size = 0;
  bool invert_colors = false;
  bool mirror_x = false;
  bool mirror_y = false;
};

class St7701sRgbDisplay final
    : public brick::interfaces::display::IDisplayDevice {
 public:
  explicit St7701sRgbDisplay(St7701sRgbPanelConfig config)
      : config_(config) {}

  ~St7701sRgbDisplay() override {
    if (panel_ != nullptr) esp_lcd_panel_del(panel_);
    if (spi_device_ != nullptr) spi_bus_remove_device(spi_device_);
    if (spi_ready_) spi_bus_free(config_.spi_host);
  }

  bool begin() override {
    if (initialized_) return true;
    if (config_.width == 0 || config_.height == 0) return false;

    ESP_LOGI(TAG, "begin ST7701S RGB %ux%u pclk=%luHz",
             config_.width, config_.height,
             static_cast<unsigned long>(config_.pixel_clock_hz));
    if (!begin_spi_() || !send_init_sequence_()) return false;
    if (!begin_rgb_panel_()) return false;
    initialized_ = true;
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
      ESP_LOGW(TAG, "ST7701S RGB profile supports only rotation 0");
      return false;
    }
    return true;
  }

  bool draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                   std::uint16_t height, const std::uint8_t* pixels,
                   std::size_t byte_count) override {
    if (!initialized_ || panel_ == nullptr || pixels == nullptr ||
        width == 0 || height == 0 || x + width > config_.width ||
        y + height > config_.height ||
        byte_count < static_cast<std::size_t>(width) * height * 2) {
      return false;
    }
    return esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height,
                                     pixels) == ESP_OK;
  }

 private:
  bool begin_spi_() {
    spi_bus_config_t bus = {};
    bus.sclk_io_num = config_.spi_sclk_gpio;
    bus.mosi_io_num = config_.spi_mosi_gpio;
    bus.miso_io_num = config_.spi_miso_gpio;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 16;
    auto err = spi_bus_initialize(config_.spi_host,
                                  &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
      return false;
    }
    spi_ready_ = err == ESP_OK;

    spi_device_interface_config_t device = {};
    device.clock_speed_hz = static_cast<int>(config_.spi_clock_hz);
    device.mode = config_.spi_mode;
    device.spics_io_num = config_.spi_cs_gpio;
    device.queue_size = 1;
    err = spi_bus_add_device(config_.spi_host,
                             &device, &spi_device_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "ST7701S SPI device init failed: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  bool write_9bit_(std::uint16_t value) {
    spi_transaction_ext_t transaction = {};
    transaction.base.flags = SPI_TRANS_VARIABLE_CMD;
    transaction.command_bits = 9;
    transaction.base.cmd = value;
    auto err = spi_device_polling_transmit(
        spi_device_, reinterpret_cast<spi_transaction_t*>(&transaction));
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "ST7701S SPI write failed: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  bool send_command_(std::uint8_t command, const std::uint8_t* data,
                     std::size_t length) {
    if (!write_9bit_(command)) return false;
    for (std::size_t index = 0; index < length; ++index) {
      if (!write_9bit_(static_cast<std::uint16_t>(data[index]) | 0x100))
        return false;
    }
    return true;
  }

  bool send_init_sequence_() {
    std::size_t index = 0;
    while (index < config_.init_sequence_size) {
      if (config_.init_sequence_size - index < 2) return false;
      const auto command = config_.init_sequence[index++];
      const auto count = config_.init_sequence[index++];
      if (count == 0xFF) {
        vTaskDelay(pdMS_TO_TICKS(command));
        continue;
      }
      if (config_.init_sequence_size - index < count ||
          !send_command_(command, config_.init_sequence + index, count)) {
        ESP_LOGE(TAG, "ST7701S init command 0x%02X failed", command);
        return false;
      }
      index += count;
      vTaskDelay(1);
    }

    constexpr std::uint8_t bank0[] = {0x77, 0x01, 0x00, 0x00, 0x10};
    constexpr std::uint8_t direction[] = {0x00};
    const std::uint8_t madctl[] = {
        static_cast<std::uint8_t>((config_.mirror_x ? 0x04 : 0x00) |
                                   (config_.mirror_y ? 0x10 : 0x00))};
    if (!send_command_(0xFF, bank0, sizeof(bank0)) ||
        !send_command_(0xC7, direction, sizeof(direction)) ||
        !send_command_(0x36, madctl, sizeof(madctl)) ||
        !send_command_(config_.invert_colors ? 0x21 : 0x20, nullptr, 0)) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!send_command_(0x11, nullptr, 0) ||
        !send_command_(0x29, nullptr, 0)) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
  }

  bool begin_rgb_panel_() {
    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_PLL160M;
    panel_config.timings.pclk_hz = config_.pixel_clock_hz;
    panel_config.timings.h_res = config_.width;
    panel_config.timings.v_res = config_.height;
    panel_config.timings.hsync_pulse_width = config_.hsync_pulse_width;
    panel_config.timings.hsync_back_porch = config_.hsync_back_porch;
    panel_config.timings.hsync_front_porch = config_.hsync_front_porch;
    panel_config.timings.vsync_pulse_width = config_.vsync_pulse_width;
    panel_config.timings.vsync_back_porch = config_.vsync_back_porch;
    panel_config.timings.vsync_front_porch = config_.vsync_front_porch;
    panel_config.timings.flags.pclk_active_neg = config_.pclk_active_neg;
    panel_config.data_width = 16;
    panel_config.bits_per_pixel = 16;
    panel_config.num_fbs = 1;
    panel_config.bounce_buffer_size_px = config_.width * 10;
    panel_config.flags.fb_in_psram = 1;
    panel_config.hsync_gpio_num = config_.hsync_gpio;
    panel_config.vsync_gpio_num = config_.vsync_gpio;
    panel_config.de_gpio_num = config_.de_gpio;
    panel_config.pclk_gpio_num = config_.pclk_gpio;
    panel_config.disp_gpio_num = GPIO_NUM_NC;
    for (std::size_t index = 0; index < config_.data_gpios.size(); ++index)
      panel_config.data_gpio_nums[index] = config_.data_gpios[index];

    auto err = esp_lcd_new_rgb_panel(&panel_config, &panel_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "RGB panel creation failed: %s", esp_err_to_name(err));
      return false;
    }
    if (config_.reset_gpio != GPIO_NUM_NC) {
      gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
      gpio_set_level(config_.reset_gpio, 0);
      vTaskDelay(pdMS_TO_TICKS(5));
      gpio_set_level(config_.reset_gpio, 1);
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    err = esp_lcd_panel_reset(panel_);
    if (err == ESP_OK) err = esp_lcd_panel_init(panel_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  St7701sRgbPanelConfig config_;
  esp_lcd_panel_handle_t panel_ = nullptr;
  spi_device_handle_t spi_device_ = nullptr;
  bool spi_ready_ = false;
  bool initialized_ = false;
  static constexpr const char* TAG = "brick_st7701s_rgb";
};

}  // namespace brick::platform::esp32::s3
