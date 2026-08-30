#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32
{

struct Ili9341SpiDisplayConfig
{
    std::uint16_t     width                 = 320;
    std::uint16_t     height                = 240;
    spi_host_device_t spi_host              = SPI2_HOST;
    gpio_num_t        sclk_gpio             = GPIO_NUM_NC;
    gpio_num_t        mosi_gpio             = GPIO_NUM_NC;
    gpio_num_t        miso_gpio             = GPIO_NUM_NC;
    gpio_num_t        cs_gpio               = GPIO_NUM_NC;
    gpio_num_t        dc_gpio               = GPIO_NUM_NC;
    gpio_num_t        backlight_gpio        = GPIO_NUM_NC;
    bool              backlight_active_high = true;
    int               spi_mode              = 0;
    std::uint32_t     spi_clock_hz          = 40'000'000;
    bool              bgr                   = true;
    std::uint8_t      madctl                = 0x28;
};

class Ili9341SpiDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit Ili9341SpiDisplay(Ili9341SpiDisplayConfig config);
    ~Ili9341SpiDisplay() override;
    bool                                            begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    bool                                            set_rotation(brick::interfaces::display::Rotation rotation) override;
    bool                                            draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer) override;

private:
    bool begin_spi_();
    bool transmit_(bool data, const std::uint8_t* bytes, std::size_t length);
    bool send_command_(std::uint8_t command);
    bool send_command_(std::uint8_t command, std::initializer_list<std::uint8_t> args);
    bool send_command_(std::uint8_t command, const std::uint8_t* args, std::size_t length);
    bool send_data_(const std::uint8_t* data, std::size_t length);
    bool set_address_window_(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height);
    bool initialize_panel_();

    Ili9341SpiDisplayConfig config_;
    spi_device_handle_t     spi_device_ = nullptr;
    bool                    spi_ready_  = false;
    bool                    started_    = false;
    alignas(4) std::array<std::uint8_t, 4096> pixel_tx_buffer_{};
    static constexpr const char* TAG = "brick_ili9341_spi";
};

}  // namespace brick::platform::esp32
