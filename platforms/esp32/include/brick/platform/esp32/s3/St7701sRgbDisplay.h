#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

namespace brick::platform::esp32::s3
{

struct St7701sRgbPanelConfig
{
    std::uint16_t width             = 0;
    std::uint16_t height            = 0;
    std::uint32_t pixel_clock_hz    = 12'000'000;
    std::uint16_t hsync_pulse_width = 10;
    std::uint16_t hsync_back_porch  = 10;
    std::uint16_t hsync_front_porch = 20;
    std::uint16_t vsync_pulse_width = 10;
    std::uint16_t vsync_back_porch  = 10;
    std::uint16_t vsync_front_porch = 20;
    bool          pclk_active_neg   = false;

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

class St7701sRgbDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit St7701sRgbDisplay(St7701sRgbPanelConfig config);
    ~St7701sRgbDisplay() override;

    bool                                    begin() override;
    brick::interfaces::display::DisplaySize size() const override;
    brick::interfaces::display::PixelFormat pixel_format() const override;
    bool                                    set_rotation(brick::interfaces::display::Rotation rotation) override;
    bool                                    draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height, const std::uint8_t* pixels, std::size_t byte_count) override;

private:
    bool begin_spi_();
    bool write_9bit_(std::uint16_t value);
    bool send_command_(std::uint8_t command, const std::uint8_t* data, std::size_t length);
    bool send_init_sequence_();
    bool begin_rgb_panel_();

    St7701sRgbPanelConfig        config_;
    esp_lcd_panel_handle_t       panel_       = nullptr;
    spi_device_handle_t          spi_device_  = nullptr;
    bool                         spi_ready_   = false;
    bool                         initialized_ = false;
    static constexpr const char* TAG          = "brick_st7701s_rgb";
};

}  // namespace brick::platform::esp32::s3
