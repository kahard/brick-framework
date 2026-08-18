#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/core/input/TouchMapper.h"
#include "brick/interfaces/display/ITouchscreen.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace brick::platform::esp32::touch
{

struct Xpt2046Config
{
    spi_host_device_t                       spi_host       = SPI3_HOST;
    gpio_num_t                              sclk_gpio      = GPIO_NUM_NC;
    gpio_num_t                              mosi_gpio      = GPIO_NUM_NC;
    gpio_num_t                              miso_gpio      = GPIO_NUM_NC;
    gpio_num_t                              cs_gpio        = GPIO_NUM_NC;
    gpio_num_t                              interrupt_gpio = GPIO_NUM_NC;
    std::uint32_t                           spi_clock_hz   = 2'000'000;
    std::uint16_t                           threshold      = 400;
    brick::interfaces::display::DisplaySize display_size{};
    brick::core::input::TouchCalibration    calibration{};
};

class Xpt2046Touchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    explicit Xpt2046Touchscreen(Xpt2046Config config);
    ~Xpt2046Touchscreen() override;
    bool begin() override;
    bool read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;

private:
    bool read_sample_(std::uint8_t command, std::uint16_t& value);

    Xpt2046Config                          config_;
    spi_device_handle_t                    spi_device_ = nullptr;
    brick::core::input::TouchMapper        mapper_{ {}, {} };
    brick::interfaces::display::TouchPoint last_point_{};
    bool                                   spi_ready_ = false;
    bool                                   started_   = false;
    bool                                   active_    = false;
};

}  // namespace brick::platform::esp32::touch
