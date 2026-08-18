#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/core/input/TouchMapper.h"
#include "brick/interfaces/display/ITouchscreen.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace brick::platform::esp32::touch
{

struct Gt911Config
{
    i2c_port_t                              port           = I2C_NUM_0;
    gpio_num_t                              sda_gpio       = GPIO_NUM_NC;
    gpio_num_t                              scl_gpio       = GPIO_NUM_NC;
    gpio_num_t                              reset_gpio     = GPIO_NUM_NC;
    gpio_num_t                              interrupt_gpio = GPIO_NUM_NC;
    std::uint8_t                            address        = 0x5D;
    std::uint32_t                           clock_hz       = 400000;
    brick::interfaces::display::DisplaySize display_size{};
    brick::core::input::TouchCalibration    calibration{};
    bool                                    read_calibration_from_controller = true;
};

class Gt911Touchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    explicit Gt911Touchscreen(Gt911Config config);
    bool begin() override;

    bool read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;

private:
    static constexpr std::size_t kMaxTouches = 5;
    static constexpr TickType_t  kTimeout    = pdMS_TO_TICKS(100);

    bool probe_();
    bool read_calibration_();
    bool write_register_(std::uint16_t reg, std::uint8_t value);
    bool read_register_(std::uint16_t reg, std::uint8_t* data, std::size_t length);

    Gt911Config                                                     config_;
    brick::core::input::TouchMapper                                 mapper_{ {}, {} };
    std::array<bool, kMaxTouches>                                   active_{};
    std::array<brick::interfaces::display::TouchPoint, kMaxTouches> last_points_{};
    bool                                                            started_ = false;
};

}  // namespace brick::platform::esp32::touch
