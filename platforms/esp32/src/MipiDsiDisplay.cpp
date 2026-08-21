#include "brick/platform/esp32/p4/MipiDsiDisplay.h"

#include "esp_cache.h"
#include "esp_memory_utils.h"

namespace brick::platform::esp32::p4
{

MipiDsiDisplay::MipiDsiDisplay(MipiDsiPanelConfig config) : config_(config)
{
}

MipiDsiDisplay::~MipiDsiDisplay()
{
    if (color_trans_done_ != nullptr)
        vSemaphoreDelete(color_trans_done_);
    if (refresh_done_ != nullptr)
        vSemaphoreDelete(refresh_done_);
    if (panel_ != nullptr)
        esp_lcd_panel_del(panel_);
    if (io_ != nullptr)
        esp_lcd_panel_io_del(io_);
    if (bus_ != nullptr)
        esp_lcd_del_dsi_bus(bus_);
    if (mipi_dsi_ldo_ != nullptr)
        esp_ldo_release_channel(mipi_dsi_ldo_);
}

bool IRAM_ATTR MipiDsiDisplay::on_color_trans_done_(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void* user_ctx)
{
    auto* self = static_cast<MipiDsiDisplay*>(user_ctx);
    BaseType_t woken = pdFALSE;
    if (self->color_trans_done_ != nullptr)
        xSemaphoreGiveFromISR(self->color_trans_done_, &woken);
    return woken == pdTRUE;
}

bool IRAM_ATTR MipiDsiDisplay::on_refresh_done_(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void* user_ctx)
{
    auto* self = static_cast<MipiDsiDisplay*>(user_ctx);
    if (!self->color_ready_for_refresh_)
        return false;
    self->color_ready_for_refresh_ = false;
    BaseType_t woken = pdFALSE;
    if (self->refresh_done_ != nullptr)
        xSemaphoreGiveFromISR(self->refresh_done_, &woken);
    return woken == pdTRUE;
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
    dpi_config.in_color_format                = LCD_COLOR_FMT_RGB565;
    dpi_config.out_color_format               = LCD_COLOR_FMT_RGB565;
    dpi_config.num_fbs                        = 1;
    dpi_config.flags.use_dma2d                = true;
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
        gpio_set_level(config_.reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(config_.reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(config_.reset_gpio, 1);
    }
    if (esp_lcd_panel_init(panel_) != ESP_OK || !send_init_sequence_() || !apply_rotation_())
        return false;
    color_trans_done_ = xSemaphoreCreateBinary();
    refresh_done_ = xSemaphoreCreateBinary();
    if (color_trans_done_ == nullptr || refresh_done_ == nullptr)
        return false;
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
    callbacks.on_color_trans_done = on_color_trans_done_;
    callbacks.on_refresh_done = on_refresh_done_;
    if (esp_lcd_dpi_panel_register_event_callbacks(panel_, &callbacks, this) != ESP_OK)
        return false;
    if (esp_lcd_panel_disp_on_off(panel_, true) != ESP_OK)
        ESP_LOGW(TAG, "panel display-on command is not supported; relying on panel init sequence");
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

bool MipiDsiDisplay::draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer)
{
    if (!initialized_ || panel_ == nullptr || area.empty() || area.x < 0 || area.y < 0 || area.x + area.width > config_.width || area.y + area.height > config_.height ||
        !buffer.valid() || buffer.width != static_cast<std::uint32_t>(area.width) || buffer.height != static_cast<std::uint32_t>(area.height) ||
        buffer.format != pixel_format() || buffer.stride_bytes != static_cast<std::size_t>(area.width) * 2)
        return false;
    const auto bytes = buffer.stride_bytes * static_cast<std::size_t>(area.height);
    if (esp_ptr_external_ram(buffer.data) &&
        esp_cache_msync(const_cast<std::uint8_t*>(buffer.data), bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED) != ESP_OK)
        return false;
    const auto err = esp_lcd_panel_draw_bitmap(panel_, area.x, area.y, area.x + area.width, area.y + area.height, buffer.data);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "draw_bitmap area=(%d,%d)-(%d,%d) failed: %s", area.x, area.y, area.x + area.width, area.y + area.height, esp_err_to_name(err));
    return err == ESP_OK;
}

bool MipiDsiDisplay::wait_for_transfer_complete(std::uint32_t timeout_ms)
{
    if (color_trans_done_ != nullptr && refresh_done_ != nullptr)
    {
        if (xSemaphoreTake(color_trans_done_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
            return false;
        color_ready_for_refresh_ = true;
        return xSemaphoreTake(refresh_done_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    }

    // esp_lcd's DPI draw call queues the source buffer for scan-out. Keep the
    // LVGL buffer alive for at least one complete video frame before reuse.
    const auto h_total = static_cast<std::uint32_t>(config_.width) + config_.hsync_back_porch + config_.hsync_front_porch + config_.hsync_pulse_width;
    const auto v_total = static_cast<std::uint32_t>(config_.height) + config_.vsync_back_porch + config_.vsync_front_porch + config_.vsync_pulse_width;
    const auto frame_us = static_cast<std::uint32_t>((static_cast<double>(h_total) * v_total * 1'000'000.0) /
                                                     (config_.pixel_clock_mhz * 1'000'000.0)) + 2'000;
    const auto wait_ms = (frame_us + 999) / 1'000;
    if (wait_ms > timeout_ms)
        return false;
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
    return true;
}

bool MipiDsiDisplay::send_init_sequence_()
{
    std::size_t index = 0;
    bool sleep_out_waited = false;
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
        if (command == 0x11 && !sleep_out_waited)
        {
            vTaskDelay(pdMS_TO_TICKS(120));
            sleep_out_waited = true;
        }
        if (config_.init_sequence_size - index < count || esp_lcd_panel_io_tx_param(io_, command, config_.init_sequence + index, count) != ESP_OK)
            return false;
        index += count;
        vTaskDelay(pdMS_TO_TICKS(command == 0x11 ? 10 : 1));
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
    // DPI panels such as the JC1060 do not implement swap_xy.  In the
    // default orientation there is no controller operation to perform.
    if (!swap && !mirror_x && !mirror_y)
        return true;
    if (swap)
        return false;
    return esp_lcd_panel_mirror(panel_, mirror_x, mirror_y) == ESP_OK;
}

}  // namespace brick::platform::esp32::p4
