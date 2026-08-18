#include "brick/platform/esp32/p4/MipiDsiDisplay.h"

namespace brick::platform::esp32::p4
{

MipiDsiDisplay::MipiDsiDisplay(MipiDsiPanelConfig config) : config_(config)
{
}

MipiDsiDisplay::~MipiDsiDisplay()
{
    if (panel_ != nullptr)
        esp_lcd_panel_del(panel_);
    if (io_ != nullptr)
        esp_lcd_panel_io_del(io_);
    if (bus_ != nullptr)
        esp_lcd_del_dsi_bus(bus_);
    if (mipi_dsi_ldo_ != nullptr)
        esp_ldo_release_channel(mipi_dsi_ldo_);
}

bool MipiDsiDisplay::begin()
{
    if (initialized_)
        return true;
    if (config_.width == 0 || config_.height == 0)
        return false;
    if (config_.mipi_dsi_ldo_channel >= 0 && config_.mipi_dsi_ldo_voltage_mv > 0)
    {
        esp_ldo_channel_config_t ldo_config = { .chan_id = config_.mipi_dsi_ldo_channel, .voltage_mv = config_.mipi_dsi_ldo_voltage_mv };
        if (esp_ldo_acquire_channel(&ldo_config, &mipi_dsi_ldo_) != ESP_OK)
            return false;
    }
    esp_lcd_dsi_bus_config_t bus_config = {};
    bus_config.bus_id                   = 0;
    bus_config.num_data_lanes           = config_.data_lanes;
    bus_config.phy_clk_src              = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_config.lane_bit_rate_mbps       = config_.lane_bit_rate_mbps;
    auto err                            = esp_lcd_new_dsi_bus(&bus_config, &bus_);
    if (err != ESP_OK)
        return false;
    esp_lcd_dbi_io_config_t io_config = {};
    io_config.virtual_channel         = 0;
    io_config.lcd_cmd_bits            = 8;
    io_config.lcd_param_bits          = 8;
    if ((err = esp_lcd_new_panel_io_dbi(bus_, &io_config, &io_)) != ESP_OK)
        return false;
    esp_lcd_dpi_panel_config_t dpi_config     = {};
    dpi_config.virtual_channel                = 0;
    dpi_config.dpi_clk_src                    = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_config.dpi_clock_freq_mhz             = config_.pixel_clock_mhz;
    dpi_config.pixel_format                   = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi_config.num_fbs                        = 1;
    dpi_config.video_timing.h_size            = config_.width;
    dpi_config.video_timing.v_size            = config_.height;
    dpi_config.video_timing.hsync_pulse_width = config_.hsync_pulse_width;
    dpi_config.video_timing.hsync_back_porch  = config_.hsync_back_porch;
    dpi_config.video_timing.hsync_front_porch = config_.hsync_front_porch;
    dpi_config.video_timing.vsync_pulse_width = config_.vsync_pulse_width;
    dpi_config.video_timing.vsync_back_porch  = config_.vsync_back_porch;
    dpi_config.video_timing.vsync_front_porch = config_.vsync_front_porch;
    if ((err = esp_lcd_new_panel_dpi(bus_, &dpi_config, &panel_)) != ESP_OK)
        return false;
    if (config_.reset_gpio != GPIO_NUM_NC)
    {
        gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config_.reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(config_.reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    if (esp_lcd_panel_init(panel_) != ESP_OK || !send_init_sequence_() || !apply_rotation_())
        return false;
    initialized_ = true;
    return true;
}

brick::interfaces::display::DisplaySize MipiDsiDisplay::size() const
{
    return { config_.width, config_.height };
}

brick::interfaces::display::PixelFormat MipiDsiDisplay::pixel_format() const
{
    return brick::interfaces::display::PixelFormat::rgb565;
}

bool MipiDsiDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    rotation_ = rotation;
    return !initialized_ || apply_rotation_();
}

bool MipiDsiDisplay::draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height, const std::uint8_t* pixels, std::size_t byte_count)
{
    if (!initialized_ || pixels == nullptr || width == 0 || height == 0 || x + width > config_.width || y + height > config_.height || byte_count < static_cast<std::size_t>(width) * height * 2)
        return false;
    return esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height, pixels) == ESP_OK;
}

bool MipiDsiDisplay::send_init_sequence_()
{
    std::size_t index = 0;
    while (index < config_.init_sequence_size)
    {
        if (config_.init_sequence_size - index < 2)
            return false;
        const auto command = config_.init_sequence[index++], count = config_.init_sequence[index++];
        if (count == 0xFF)
        {
            vTaskDelay(pdMS_TO_TICKS(command));
            continue;
        }
        if (config_.init_sequence_size - index < count || esp_lcd_panel_io_tx_param(io_, command, config_.init_sequence + index, count) != ESP_OK)
            return false;
        index += count;
        vTaskDelay(1);
    }
    return true;
}

bool MipiDsiDisplay::apply_rotation_()
{
    bool swap = false, mirror_x = false, mirror_y = false;
    switch (rotation_)
    {
        case brick::interfaces::display::Rotation::rotate_90:
            swap     = true;
            mirror_x = true;
            break;
        case brick::interfaces::display::Rotation::rotate_180:
            mirror_x = true;
            mirror_y = true;
            break;
        case brick::interfaces::display::Rotation::rotate_270:
            swap     = true;
            mirror_y = true;
            break;
        default:
            break;
    }
    return esp_lcd_panel_swap_xy(panel_, swap) == ESP_OK && esp_lcd_panel_mirror(panel_, mirror_x, mirror_y) == ESP_OK;
}

}  // namespace brick::platform::esp32::p4
