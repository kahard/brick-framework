#include "brick/platform/esp32/touch/Gsl3680Touchscreen.h"

#include "esp_lcd_touch_gsl3680.h"
#include "esp_log.h"

namespace brick::platform::esp32::touch
{

namespace
{
    constexpr char TAG[] = "brick_gsl3680";
}

Gsl3680Touchscreen::Gsl3680Touchscreen(Gsl3680Config config) : config_(config)
{
}

Gsl3680Touchscreen::~Gsl3680Touchscreen()
{
    if (touch_ != nullptr)
        esp_lcd_touch_del(touch_);
    if (io_ != nullptr)
        esp_lcd_panel_io_del(io_);
    if (bus_ != nullptr)
        i2c_del_master_bus(bus_);
}

bool Gsl3680Touchscreen::begin()
{
    if (started_)
        return true;
    if (config_.sda_gpio == GPIO_NUM_NC || config_.scl_gpio == GPIO_NUM_NC || config_.display_size.width == 0 || config_.display_size.height == 0)
        return false;

    i2c_master_bus_config_t bus_config      = {};
    bus_config.clk_source                   = I2C_CLK_SRC_DEFAULT;
    bus_config.i2c_port                     = I2C_NUM_1;
    bus_config.sda_io_num                   = config_.sda_gpio;
    bus_config.scl_io_num                   = config_.scl_gpio;
    bus_config.glitch_ignore_cnt            = 7;
    bus_config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus_config, &bus_) != ESP_OK)
        return false;

    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr                      = ESP_LCD_TOUCH_IO_I2C_GSL3680_ADDRESS;
    io_config.control_phase_bytes           = 1;
    io_config.dc_bit_offset                 = 0;
    io_config.lcd_cmd_bits                  = 8;
    io_config.lcd_param_bits                = 8;
    io_config.flags.disable_control_phase   = 1;
    io_config.scl_speed_hz                  = config_.clock_hz;
    if (esp_lcd_new_panel_io_i2c(bus_, &io_config, &io_) != ESP_OK)
        return false;

    esp_lcd_touch_io_gsl3680_config_t gsl_config = {};
    gsl_config.dev_addr                          = ESP_LCD_TOUCH_IO_I2C_GSL3680_ADDRESS;
    esp_lcd_touch_config_t touch_config          = {};
    touch_config.x_max                           = config_.display_size.width;
    touch_config.y_max                           = config_.display_size.height;
    touch_config.rst_gpio_num                    = config_.reset_gpio;
    touch_config.int_gpio_num                    = config_.interrupt_gpio;
    touch_config.driver_data                     = &gsl_config;
    touch_config.levels.reset                    = 0;
    touch_config.levels.interrupt                = 0;
    touch_config.flags.mirror_x                  = config_.mirror_x;
    touch_config.flags.mirror_y                  = config_.mirror_y;
    touch_config.flags.swap_xy                   = config_.swap_xy;
    if (esp_lcd_touch_new_i2c_gsl3680(io_, &touch_config, &touch_) != ESP_OK)
        return false;
    started_ = true;
    ESP_LOGI(TAG, "GSL3680 initialized: %ux%u", config_.display_size.width, config_.display_size.height);
    return true;
}

bool Gsl3680Touchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (!started_ || touch_ == nullptr || points == nullptr || capacity == 0)
        return false;
    if (esp_lcd_touch_read_data(touch_) != ESP_OK)
        return false;

    esp_lcd_touch_point_data_t data[5]     = {};
    std::uint8_t               point_count = 0;
    if (esp_lcd_touch_get_data(touch_, data, &point_count, 5) != ESP_OK)
        return false;
    std::array<bool, 5> seen{};
    for (std::size_t i = 0; i < point_count && count < capacity; ++i)
    {
        const auto id    = data[i].track_id < active_.size() ? data[i].track_id : static_cast<std::uint8_t>(i);
        auto&      point = points[count++];
        point.id         = id;
        point.x          = static_cast<std::int16_t>(data[i].x);
        point.y          = static_cast<std::int16_t>(data[i].y);
        point.raw_x      = point.x;
        point.raw_y      = point.y;
        point.pressure   = static_cast<std::int16_t>(data[i].strength);
        point.state      = active_[id] ? brick::interfaces::display::TouchState::moved : brick::interfaces::display::TouchState::pressed;
        seen[id]         = true;
    }
    for (std::size_t id = 0; id < active_.size() && count < capacity; ++id)
    {
        if (active_[id] && !seen[id])
        {
            auto point      = last_points_[id];
            point.state     = brick::interfaces::display::TouchState::released;
            points[count++] = point;
        }
    }
    active_ = seen;
    for (std::size_t i = 0; i < count; ++i)
        if (points[i].state != brick::interfaces::display::TouchState::released)
            last_points_[points[i].id] = points[i];
    return true;
}

}  // namespace brick::platform::esp32::touch
