#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/display_device.hpp"
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32::p4 {

struct MipiDsiPanelConfig {
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::uint8_t data_lanes = 2;
  float lane_bit_rate_mbps = 1500.0f;
  float pixel_clock_mhz = 16.0f;
  std::uint16_t hsync_pulse_width = 10;
  std::uint16_t hsync_back_porch = 10;
  std::uint16_t hsync_front_porch = 20;
  std::uint16_t vsync_pulse_width = 10;
  std::uint16_t vsync_back_porch = 10;
  std::uint16_t vsync_front_porch = 10;
  gpio_num_t reset_gpio = GPIO_NUM_NC;
  int mipi_dsi_ldo_channel = -1;
  int mipi_dsi_ldo_voltage_mv = 0;

  // Sequence format: command, argument_count, arguments...
  // A command with argument_count 0xFF means delay command milliseconds.
  const std::uint8_t* init_sequence = nullptr;
  std::size_t init_sequence_size = 0;
};

class MipiDsiDisplay final : public brick::interfaces::display::IDisplayDevice {
 public:
  explicit MipiDsiDisplay(MipiDsiPanelConfig config) : config_(config) {}

  ~MipiDsiDisplay() override {
    if (panel_ != nullptr) esp_lcd_panel_del(panel_);
    if (io_ != nullptr) esp_lcd_panel_io_del(io_);
    if (bus_ != nullptr) esp_lcd_del_dsi_bus(bus_);
    if (mipi_dsi_ldo_ != nullptr) esp_ldo_release_channel(mipi_dsi_ldo_);
  }

  bool begin() override {
    if (initialized_) return true;
    if (config_.width == 0 || config_.height == 0) return false;

    ESP_LOGI(TAG, "begin %ux%u lanes=%u lane_rate=%.0fMbps pclk=%.0fMHz",
             config_.width, config_.height, config_.data_lanes,
             config_.lane_bit_rate_mbps, config_.pixel_clock_mhz);

    if (config_.mipi_dsi_ldo_channel >= 0 &&
        config_.mipi_dsi_ldo_voltage_mv > 0) {
      esp_ldo_channel_config_t ldo_config = {};
      ldo_config.chan_id = config_.mipi_dsi_ldo_channel;
      ldo_config.voltage_mv = config_.mipi_dsi_ldo_voltage_mv;
      auto ldo_err = esp_ldo_acquire_channel(&ldo_config, &mipi_dsi_ldo_);
      if (ldo_err != ESP_OK) {
        ESP_LOGE(TAG, "MIPI DSI LDO failed: %s", esp_err_to_name(ldo_err));
        return false;
      }
      ESP_LOGI(TAG, "MIPI DSI LDO channel %d @ %dmV ready",
               config_.mipi_dsi_ldo_channel,
               config_.mipi_dsi_ldo_voltage_mv);
    }

    esp_lcd_dsi_bus_config_t bus_config = {};
    bus_config.bus_id = 0;
    bus_config.num_data_lanes = config_.data_lanes;
    // ECO1/ECO2 use the legacy PLL_F20M reference source. The newer XTAL
    // selector is not supported by the pre-v3 P4 PHY.
    bus_config.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_config.lane_bit_rate_mbps = config_.lane_bit_rate_mbps;
    auto err = esp_lcd_new_dsi_bus(&bus_config, &bus_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "new DSI bus failed: %s", esp_err_to_name(err));
      return false;
    }
    ESP_LOGI(TAG, "DSI bus ready");

    esp_lcd_dbi_io_config_t io_config = {};
    io_config.virtual_channel = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    err = esp_lcd_new_panel_io_dbi(bus_, &io_config, &io_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "new DBI IO failed: %s", esp_err_to_name(err));
      return false;
    }
    ESP_LOGI(TAG, "DBI IO ready");

    esp_lcd_dpi_panel_config_t dpi_config = {};
    dpi_config.virtual_channel = 0;
    dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_config.dpi_clock_freq_mhz = config_.pixel_clock_mhz;
    dpi_config.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi_config.num_fbs = 1;
    dpi_config.video_timing.h_size = config_.width;
    dpi_config.video_timing.v_size = config_.height;
    dpi_config.video_timing.hsync_pulse_width = config_.hsync_pulse_width;
    dpi_config.video_timing.hsync_back_porch = config_.hsync_back_porch;
    dpi_config.video_timing.hsync_front_porch = config_.hsync_front_porch;
    dpi_config.video_timing.vsync_pulse_width = config_.vsync_pulse_width;
    dpi_config.video_timing.vsync_back_porch = config_.vsync_back_porch;
    dpi_config.video_timing.vsync_front_porch = config_.vsync_front_porch;
    err = esp_lcd_new_panel_dpi(bus_, &dpi_config, &panel_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "new DPI panel failed: %s", esp_err_to_name(err));
      return false;
    }
    ESP_LOGI(TAG, "DPI panel ready");

    if (config_.reset_gpio != GPIO_NUM_NC) {
      ESP_LOGI(TAG, "reset GPIO %d", config_.reset_gpio);
      gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
      gpio_set_level(config_.reset_gpio, 0);
      vTaskDelay(pdMS_TO_TICKS(5));
      gpio_set_level(config_.reset_gpio, 1);
      vTaskDelay(pdMS_TO_TICKS(120));
    }
    ESP_LOGI(TAG, "panel init");
    err = esp_lcd_panel_init(panel_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
      return false;
    }
    ESP_LOGI(TAG, "panel init done; sending vendor sequence");
    if (!send_init_sequence_()) return false;
    ESP_LOGI(TAG, "vendor sequence done; applying rotation");
    if (!apply_rotation_()) return false;
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
    rotation_ = rotation;
    return !initialized_ || apply_rotation_();
  }

  bool draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                   std::uint16_t height, const std::uint8_t* pixels,
                   std::size_t byte_count) override {
    if (!initialized_ || pixels == nullptr || width == 0 || height == 0 ||
        x + width > config_.width || y + height > config_.height ||
        byte_count < static_cast<std::size_t>(width) * height * 2) {
      return false;
    }
    return esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height,
                                     pixels) == ESP_OK;
  }

 private:
  bool send_init_sequence_() {
    std::size_t index = 0;
    while (index < config_.init_sequence_size) {
      if (config_.init_sequence_size - index < 2) return false;
      const auto command = config_.init_sequence[index++];
      const auto argument_count = config_.init_sequence[index++];
      if (argument_count == 0xFF) {
        ESP_LOGI(TAG, "init delay %ums", command);
        vTaskDelay(pdMS_TO_TICKS(command));
        continue;
      }
      if (config_.init_sequence_size - index < argument_count) return false;
      if (esp_lcd_panel_io_tx_param(io_, command,
                                    config_.init_sequence + index,
                                    argument_count) != ESP_OK) {
        ESP_LOGE(TAG, "init command 0x%02X failed", command);
        return false;
      }
      index += argument_count;
      vTaskDelay(1);
    }
    return true;
  }

  bool apply_rotation_() {
    if (rotation_ == brick::interfaces::display::Rotation::rotate_0) {
      return true;
    }
    bool swap = false;
    bool mirror_x = false;
    bool mirror_y = false;
    switch (rotation_) {
      case brick::interfaces::display::Rotation::rotate_0: break;
      case brick::interfaces::display::Rotation::rotate_90:
        swap = true; mirror_x = true; break;
      case brick::interfaces::display::Rotation::rotate_180:
        mirror_x = true; mirror_y = true; break;
      case brick::interfaces::display::Rotation::rotate_270:
        swap = true; mirror_y = true; break;
    }
    auto err = esp_lcd_panel_swap_xy(panel_, swap);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "panel does not support swap_xy for requested rotation");
      return false;
    }
    return esp_lcd_panel_mirror(panel_, mirror_x, mirror_y) == ESP_OK;
  }

  MipiDsiPanelConfig config_;
  brick::interfaces::display::Rotation rotation_ =
      brick::interfaces::display::Rotation::rotate_0;
  esp_lcd_dsi_bus_handle_t bus_ = nullptr;
  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  esp_ldo_channel_handle_t mipi_dsi_ldo_ = nullptr;
  bool initialized_ = false;

  static constexpr const char* TAG = "brick_mipi_dsi";
};

}  // namespace brick::platform::esp32::p4
