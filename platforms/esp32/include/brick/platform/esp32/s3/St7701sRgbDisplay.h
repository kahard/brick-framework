#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/IFrameBufferDisplay.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

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

class St7701sRgbDisplay final : public brick::interfaces::display::IDisplayDevice, public brick::interfaces::display::IFrameBufferDisplay
{
public:
    explicit St7701sRgbDisplay(St7701sRgbPanelConfig config);
    ~St7701sRgbDisplay() override;

    bool                                            begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    bool                                            set_rotation(brick::interfaces::display::Rotation rotation) override;
    bool                                            draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer) override;
    bool                                            wait_for_transfer_complete(std::uint32_t timeout_ms) override;
    bool                                            wait_for_vsync(std::uint32_t timeout_ms) override;
    std::uint8_t                                    frame_buffer_count() const override;
    bool                                            get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer) override;
    bool                                            present_frame_buffer(std::uint8_t index) override;

private:
    static bool IRAM_ATTR on_vsync_(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx);
    static bool IRAM_ATTR on_color_trans_done_(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx);
    bool                  begin_spi_();
    bool                  write_9bit_(std::uint16_t value);
    bool                  send_command_(std::uint8_t command, const std::uint8_t* data, std::size_t length);
    bool                  send_init_sequence_();
    bool                  begin_rgb_panel_();

    St7701sRgbPanelConfig        config_;
    esp_lcd_panel_handle_t       panel_              = nullptr;
    spi_device_handle_t          spi_device_         = nullptr;
    SemaphoreHandle_t            vsync_semaphore_    = nullptr;
    SemaphoreHandle_t            transfer_semaphore_ = nullptr;
    bool                         spi_ready_          = false;
    bool                         initialized_        = false;
    static constexpr const char* TAG                 = "brick_st7701s_rgb";
};

}  // namespace brick::platform::esp32::s3
