#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "driver/gpio.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32::p4
{

struct MipiDsiPanelConfig
{
    std::uint16_t width                   = 0;
    std::uint16_t height                  = 0;
    std::uint8_t  data_lanes              = 2;
    float         lane_bit_rate_mbps      = 1500.0f;
    float         pixel_clock_mhz         = 16.0f;
    std::uint16_t hsync_pulse_width       = 10;
    std::uint16_t hsync_back_porch        = 10;
    std::uint16_t hsync_front_porch       = 20;
    std::uint16_t vsync_pulse_width       = 10;
    std::uint16_t vsync_back_porch        = 10;
    std::uint16_t vsync_front_porch       = 10;
    gpio_num_t    reset_gpio              = GPIO_NUM_NC;
    int           mipi_dsi_ldo_channel    = -1;
    int           mipi_dsi_ldo_voltage_mv = 0;

    // Sequence format: command, argument_count, arguments...
    // A command with argument_count 0xFF means delay command milliseconds.
    const std::uint8_t* init_sequence      = nullptr;
    std::size_t         init_sequence_size = 0;
};

class MipiDsiDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit MipiDsiDisplay(MipiDsiPanelConfig config);
    ~MipiDsiDisplay() override;
    bool                                    begin() override;
    brick::interfaces::display::DisplaySize size() const override;
    brick::interfaces::display::PixelFormat pixel_format() const override;
    bool                                    set_rotation(brick::interfaces::display::Rotation rotation) override;
    bool                                    draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height, const std::uint8_t* pixels, std::size_t byte_count) override;

private:
    bool send_init_sequence_();
    bool apply_rotation_();

    MipiDsiPanelConfig                   config_;
    brick::interfaces::display::Rotation rotation_     = brick::interfaces::display::Rotation::rotate_0;
    esp_lcd_dsi_bus_handle_t             bus_          = nullptr;
    esp_lcd_panel_io_handle_t            io_           = nullptr;
    esp_lcd_panel_handle_t               panel_        = nullptr;
    esp_ldo_channel_handle_t             mipi_dsi_ldo_ = nullptr;
    bool                                 initialized_  = false;

    static constexpr const char* TAG = "brick_mipi_dsi";
};

}  // namespace brick::platform::esp32::p4
