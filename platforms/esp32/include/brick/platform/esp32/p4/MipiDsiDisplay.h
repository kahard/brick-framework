#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/memory/IMemoryProvider.h"
#include "brick/platform/esp32/EspStatus.h"
#include "driver/gpio.h"
#include "driver/ppa.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace brick::platform::esp32::p4
{

struct MipiDsiPanelConfig
{
    std::uint16_t                        width                   = 0;
    std::uint16_t                        height                  = 0;
    std::uint8_t                         data_lanes              = 2;
    float                                lane_bit_rate_mbps      = 1500.0f;
    float                                pixel_clock_mhz         = 16.0f;
    std::uint16_t                        hsync_pulse_width       = 10;
    std::uint16_t                        hsync_back_porch        = 10;
    std::uint16_t                        hsync_front_porch       = 20;
    std::uint16_t                        vsync_pulse_width       = 10;
    std::uint16_t                        vsync_back_porch        = 10;
    std::uint16_t                        vsync_front_porch       = 10;
    gpio_num_t                           reset_gpio              = GPIO_NUM_NC;
    int                                  mipi_dsi_ldo_channel    = -1;
    int                                  mipi_dsi_ldo_voltage_mv = 0;
    brick::interfaces::display::Rotation default_rotation        = brick::interfaces::display::Rotation::rotate_0;

    // Sequence format: command, argument_count, arguments...
    // A command with argument_count 0xFF means delay command milliseconds.
    const std::uint8_t* init_sequence      = nullptr;
    std::size_t         init_sequence_size = 0;
};

class MipiDsiDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    /// memory provides the PSRAM rotation buffers when a rotation is used.
    MipiDsiDisplay(MipiDsiPanelConfig config, brick::interfaces::memory::IMemoryProvider& memory);
    ~MipiDsiDisplay() override;
    Status                                          begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    Status set_rotation(brick::interfaces::display::Rotation rotation) override;
    Status draw_buffer(brick::interfaces::display::DisplayRect        area,
                       const brick::interfaces::display::PixelBuffer& buffer) override;
    Status wait_for_transfer_complete(std::uint32_t timeout_ms) override;

private:
    Status                send_init_sequence_();
    Status                apply_rotation_();
    static bool IRAM_ATTR on_color_trans_done_(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void*);
    static bool IRAM_ATTR on_refresh_done_(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void*);
    Status                rotated_transfer_(brick::interfaces::display::DisplayRect        area,
                                            const brick::interfaces::display::PixelBuffer& buffer);
    Status                ensure_rotation_buffers_();
    void                  release_rotation_buffers_();
    void                  rotate_buffer_(const std::uint16_t* source);
    Status                ppa_rotate_buffer_(const std::uint16_t* source);
    Status                full_frame_transfer_(const brick::interfaces::display::PixelBuffer& buffer);

    MipiDsiPanelConfig                   config_;
    brick::interfaces::display::Rotation rotation_         = brick::interfaces::display::Rotation::rotate_0;
    esp_lcd_dsi_bus_handle_t             bus_              = nullptr;
    esp_lcd_panel_io_handle_t            io_               = nullptr;
    esp_lcd_panel_handle_t               panel_            = nullptr;
    esp_ldo_channel_handle_t             mipi_dsi_ldo_     = nullptr;
    SemaphoreHandle_t                    color_trans_done_ = nullptr;
    SemaphoreHandle_t                    refresh_done_     = nullptr;
    // Set by the task, cleared by the refresh-done interrupt.
    std::atomic<bool>                           color_ready_for_refresh_{ false };
    bool                                        initialized_      = false;
    bool                                        transfer_pending_ = false;
    std::uint16_t                               logical_width_    = 0;
    std::uint16_t                               logical_height_   = 0;
    brick::interfaces::memory::IMemoryProvider& memory_;
    std::uint16_t*                              rotation_source_         = nullptr;
    std::uint16_t*                              rotation_target_         = nullptr;
    ppa_client_handle_t                         ppa_client_              = nullptr;
    std::uint8_t*                               dpi_frame_buffers_[2]    = {};
    std::uint8_t                                dpi_active_frame_buffer_ = 0;

    static constexpr const char* TAG = "brick_mipi_dsi";
};

}  // namespace brick::platform::esp32::p4
