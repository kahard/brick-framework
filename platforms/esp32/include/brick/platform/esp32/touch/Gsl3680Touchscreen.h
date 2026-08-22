#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/ITouchscreen.h"
#include "brick/interfaces/display/DisplayTypes.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

namespace brick::platform::esp32::touch
{

struct Gsl3680Config
{
    gpio_num_t                              sda_gpio = GPIO_NUM_NC;
    gpio_num_t                              scl_gpio = GPIO_NUM_NC;
    gpio_num_t                              reset_gpio = GPIO_NUM_NC;
    gpio_num_t                              interrupt_gpio = GPIO_NUM_NC;
    std::uint32_t                           clock_hz = 100000;
    brick::interfaces::display::DisplaySize display_size{};
    bool                                    mirror_x = false;
    bool                                    mirror_y = false;
    bool                                    swap_xy = false;
};

class Gsl3680Touchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    explicit Gsl3680Touchscreen(Gsl3680Config config);
    ~Gsl3680Touchscreen() override;
    bool begin() override;
    bool read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;
private:
    Gsl3680Config config_;
    i2c_master_bus_handle_t bus_ = nullptr;
    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    bool started_ = false;
    std::array<bool, 5> active_{};
    std::array<brick::interfaces::display::TouchPoint, 5> last_points_{};
};

}  // namespace brick::platform::esp32::touch
