#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/IFrameBufferDisplay.h"
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

/// MIPI-DSI video-mode panel on the ESP32-P4 with two scan-out frame buffers
/// in PSRAM. The CPU does not copy pixels:
/// - a full frame is copied by the PPA (2D-DMA) into the hidden frame buffer,
///   which is then flipped in at the end of a refresh (tear-free); a frame
///   drawn straight into a frame buffer (IFrameBufferDisplay, rotation 0) is
///   flipped without any copy;
/// - a partial area is copied by 2D-DMA into the frame buffer on the screen;
/// - with a rotation the PPA rotates only the drawn area, straight into the
///   frame buffer, so no extra full-frame buffers are needed;
/// - fill_rect() is a PPA fill.
/// Without the PPA the CPU does the same work as a fallback.
class MipiDsiDisplay final : public brick::interfaces::display::IDisplayDevice,
                             public brick::interfaces::display::IFrameBufferDisplay
{
public:
    explicit MipiDsiDisplay(MipiDsiPanelConfig config);
    ~MipiDsiDisplay() override;
    Status                                          begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    Status set_rotation(brick::interfaces::display::Rotation rotation) override;
    Status draw_buffer(brick::interfaces::display::DisplayRect        area,
                       const brick::interfaces::display::PixelBuffer& buffer) override;
    Status wait_for_transfer_complete(std::uint32_t timeout_ms) override;
    Status wait_for_vsync(std::uint32_t timeout_ms) override;
    Status fill_rect(brick::interfaces::display::DisplayRect area, std::uint16_t color) override;

    /// Two frame buffers with rotation 0; none with a rotation, whose
    /// scan-out layout differs from the logical one.
    std::uint8_t frame_buffer_count() const override;
    Status       get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer) override;
    /// Shows the frame buffer from the next refresh on. Drawing into the other
    /// one may start after wait_for_vsync() (or wait_for_transfer_complete()).
    Status present_frame_buffer(std::uint8_t index) override;
    /// Through the PPA (2D-DMA) at rotation 0; the CPU copies when it cannot.
    /// Both copies are queued (up to kPpaQueueDepth): each returns once its
    /// copy is submitted, wait_for_transfer_complete() waits for all of them.
    Status copy_to_frame_buffer(std::uint8_t index, brick::interfaces::display::DisplayRect area,
                                const brick::interfaces::display::PixelBuffer& pixels) override;
    Status copy_between_frame_buffers(std::uint8_t from, std::uint8_t to,
                                      brick::interfaces::display::DisplayRect area) override;

private:
    /// What the last operation left running.
    enum class Pending : std::uint8_t
    {
        none,
        copy,  ///< 2D-DMA copy into the frame buffer on the screen
        flip,  ///< page flip, done at the end of the current refresh
        ppa,   ///< PPA copies into frame buffers (copy_to_frame_buffer() and
               ///< copy_between_frame_buffers())
    };

    Status                create_bus_();
    Status                create_dpi_panel_();
    void                  create_ppa_clients_();
    void                  reset_panel_();
    Status                register_callbacks_();
    Status                send_init_sequence_();
    static bool IRAM_ATTR on_color_trans_done_(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void*);
    static bool IRAM_ATTR on_refresh_done_(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void*);
    static bool IRAM_ATTR on_ppa_done_(ppa_client_handle_t, ppa_event_data_t*, void*);
    bool                  valid_area_(brick::interfaces::display::DisplayRect area) const;
    brick::interfaces::display::DisplayRect to_physical_(brick::interfaces::display::DisplayRect area) const;
    Status                                  full_frame_transfer_(const brick::interfaces::display::PixelBuffer& buffer);
    Status                                  partial_transfer_(brick::interfaces::display::DisplayRect        area,
                                                              const brick::interfaces::display::PixelBuffer& buffer);
    Status write_area_(brick::interfaces::display::DisplayRect area, const std::uint8_t* pixels,
                       std::uint8_t frame_buffer);
    Status ppa_write_bands_(brick::interfaces::display::DisplayRect area, const std::uint8_t* pixels,
                            std::uint8_t frame_buffer, bool wait = true);
    Status ppa_copy_between_(std::uint8_t from, std::uint8_t to, brick::interfaces::display::DisplayRect area);
    Status ppa_write_area_(brick::interfaces::display::DisplayRect area, const std::uint8_t* pixels,
                           std::uint8_t frame_buffer, bool wait = true);
    Status run_srm_(ppa_srm_oper_config_t& operation, brick::interfaces::display::DisplayRect area, bool wait = true);
    Status wait_for_srm_();
    Status take_srm_done_();
    Status settle_late_srm_();
    Status sync_rows_(std::uint8_t frame_buffer, brick::interfaces::display::DisplayRect area);
    void   cpu_write_area_(brick::interfaces::display::DisplayRect area, const std::uint16_t* pixels,
                           std::uint16_t* frame_buffer) const;
    Status flip_to_(std::uint8_t frame_buffer);
    Status wait_frame_end_(std::uint32_t timeout_ms);
    std::size_t frame_buffer_bytes_() const;

    MipiDsiPanelConfig                   config_;
    brick::interfaces::display::Rotation rotation_         = brick::interfaces::display::Rotation::rotate_0;
    esp_lcd_dsi_bus_handle_t             bus_              = nullptr;
    esp_lcd_panel_io_handle_t            io_               = nullptr;
    esp_lcd_panel_handle_t               panel_            = nullptr;
    esp_ldo_channel_handle_t             mipi_dsi_ldo_     = nullptr;
    SemaphoreHandle_t                    color_trans_done_ = nullptr;
    SemaphoreHandle_t                    refresh_done_     = nullptr;
    // Set by the task, cleared by the refresh-done interrupt.
    std::atomic<bool>   frame_end_wanted_{ false };
    bool                initialized_    = false;
    Pending             pending_        = Pending::none;
    std::uint16_t       logical_width_  = 0;
    std::uint16_t       logical_height_ = 0;
    ppa_client_handle_t ppa_srm_        = nullptr;
    ppa_client_handle_t ppa_fill_       = nullptr;
    std::size_t         ppa_out_bytes_  = 0;
    // Counted up by the PPA as each queued copy or rotation ends; srm_queued_
    // of them have not been counted yet. srm_late_ is set while one ran past
    // its timeout and has not ended yet: the CPU copies meanwhile.
    SemaphoreHandle_t             ppa_done_      = nullptr;
    std::uint8_t                  srm_queued_    = 0;
    bool                          srm_late_      = false;
    static constexpr std::uint8_t kPpaQueueDepth = 8;
    // The queued copies, oldest at srm_oldest_: area and first pixel addresses.
    struct SrmCopy
    {
        brick::interfaces::display::DisplayRect area{};
        std::uintptr_t                          in  = 0;
        std::uintptr_t                          out = 0;
    };
    std::array<SrmCopy, kPpaQueueDepth> srm_copies_{};
    std::uint8_t                        srm_oldest_          = 0;
    static constexpr std::uint8_t       kDpiFrameBufferCount = 2;
    static constexpr std::uint32_t      kResetPulseMs        = 5;

    std::uint8_t* dpi_frame_buffers_[kDpiFrameBufferCount] = {};
    // The frame buffer esp_lcd scans out (and copies partial areas into).
    std::uint8_t dpi_active_frame_buffer_ = 0;

    static constexpr const char* TAG = "brick_mipi_dsi";
};

}  // namespace brick::platform::esp32::p4
