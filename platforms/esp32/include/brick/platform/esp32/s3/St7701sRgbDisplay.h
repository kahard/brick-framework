#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/IFrameBufferDisplay.h"
#include "brick/platform/esp32/EspStatus.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace brick::platform::esp32::s3
{

struct St7701sRgbPanelConfig
{
    std::uint16_t width              = 0;
    std::uint16_t height             = 0;
    std::uint32_t pixel_clock_hz     = 12'000'000;
    std::uint16_t hsync_pulse_width  = 10;
    std::uint16_t hsync_back_porch   = 10;
    std::uint16_t hsync_front_porch  = 20;
    std::uint16_t vsync_pulse_width  = 10;
    std::uint16_t vsync_back_porch   = 10;
    std::uint16_t vsync_front_porch  = 20;
    bool          pclk_active_neg    = false;
    std::uint8_t  frame_buffer_count = 1;
    /// Lines of internal-RAM bounce buffers that the CPU refills from the
    /// PSRAM frame buffer in an interrupt (with any number of frame buffers).
    /// They keep PSRAM latency away from the pixel clock at the cost of a
    /// steady CPU load. 0 lets the DMA read PSRAM directly, without CPU work;
    /// the image then shifts sideways when flash and PSRAM traffic (they share
    /// one bus on the ESP32-S3) leaves the DMA short of data, e.g. while
    /// assets stream from flash at full speed. width * height must be a
    /// multiple of width * bounce_buffer_lines.
    std::uint16_t bounce_buffer_lines = 10;

    spi_host_device_t spi_host      = SPI2_HOST;
    gpio_num_t        spi_sclk_gpio = GPIO_NUM_NC;
    gpio_num_t        spi_mosi_gpio = GPIO_NUM_NC;
    gpio_num_t        spi_miso_gpio = GPIO_NUM_NC;
    gpio_num_t        spi_cs_gpio   = GPIO_NUM_NC;
    int               spi_mode      = 3;
    std::uint32_t     spi_clock_hz  = 2'000'000;

    gpio_num_t          hsync_gpio = GPIO_NUM_NC;
    gpio_num_t          vsync_gpio = GPIO_NUM_NC;
    gpio_num_t          de_gpio    = GPIO_NUM_NC;
    gpio_num_t          pclk_gpio  = GPIO_NUM_NC;
    std::array<int, 16> data_gpios{};
    gpio_num_t          reset_gpio            = GPIO_NUM_NC;
    gpio_num_t          backlight_gpio        = GPIO_NUM_NC;
    bool                backlight_active_high = true;

    // Sequence format: command, argument_count, arguments...
    // A command with argument_count 0xFF is a delay in milliseconds.
    const std::uint8_t* init_sequence      = nullptr;
    std::size_t         init_sequence_size = 0;
    bool                invert_colors      = false;
    bool                mirror_x           = false;
    bool                mirror_y           = false;
};

class St7701sRgbDisplay final : public brick::interfaces::display::IDisplayDevice,
                                public brick::interfaces::display::IFrameBufferDisplay
{
public:
    explicit St7701sRgbDisplay(St7701sRgbPanelConfig config);
    ~St7701sRgbDisplay() override;

    Status                                          begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    Status       set_rotation(brick::interfaces::display::Rotation rotation) override;
    Status       draw_buffer(brick::interfaces::display::DisplayRect        area,
                             const brick::interfaces::display::PixelBuffer& buffer) override;
    Status       wait_for_transfer_complete(std::uint32_t timeout_ms) override;
    Status       wait_for_vsync(std::uint32_t timeout_ms) override;
    std::uint8_t frame_buffer_count() const override;
    Status       get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer) override;
    Status       present_frame_buffer(std::uint8_t index) override;
    /// CPU copies (brick/core/display/FrameBufferCopy.h).
    Status copy_to_frame_buffer(std::uint8_t index, brick::interfaces::display::DisplayRect area,
                                const brick::interfaces::display::PixelBuffer& pixels) override;
    Status copy_between_frame_buffers(std::uint8_t from, std::uint8_t to,
                                      brick::interfaces::display::DisplayRect area) override;

private:
    static bool IRAM_ATTR on_vsync_(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data,
                                    void* user_ctx);
    Status                begin_spi_();
    Status                write_9bit_(std::uint16_t value);
    Status                send_command_(std::uint8_t command, const std::uint8_t* data, std::size_t length);
    Status                send_init_sequence_();
    Status                begin_rgb_panel_();
    Status                create_semaphores_();
    Status                create_rgb_panel_();
    void                  reset_panel_();
    void                  compute_frame_timing_();

    St7701sRgbPanelConfig  config_;
    esp_lcd_panel_handle_t panel_           = nullptr;
    spi_device_handle_t    spi_device_      = nullptr;
    SemaphoreHandle_t      vsync_semaphore_ = nullptr;

    // Page flips: last_vsync_us_ is the time of the last VSYNC (low 32 bits of
    // esp_timer_get_time(), set by the ISR); a flip less than late_flip_us_
    // before the next VSYNC takes effect a frame later; flip_vsyncs_ is how
    // many VSYNCs wait_for_vsync() has to see before the buffer shown before
    // the last flip is free.
    std::atomic<std::uint32_t> last_vsync_us_{ 0 };
    std::uint32_t              frame_period_us_ = 0;
    std::uint32_t              late_flip_us_    = 0;
    std::uint8_t               flip_vsyncs_     = 0;

    bool                         spi_ready_   = false;
    bool                         initialized_ = false;
    static constexpr const char* TAG          = "brick_st7701s_rgb";
};

}  // namespace brick::platform::esp32::s3
