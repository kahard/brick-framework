#include "brick/platform/esp32/p4/MipiDsiDisplay.h"

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include <cstring>

namespace brick::platform::esp32::p4
{

MipiDsiDisplay::MipiDsiDisplay(MipiDsiPanelConfig config) : config_(config), rotation_(config.default_rotation)
{
    logical_width_  = config_.width;
    logical_height_ = config_.height;
    if (rotation_ == brick::interfaces::display::Rotation::rotate_90 || rotation_ == brick::interfaces::display::Rotation::rotate_270)
    {
        logical_width_  = config_.height;
        logical_height_ = config_.width;
    }
}

MipiDsiDisplay::~MipiDsiDisplay()
{
    if (ppa_client_ != nullptr)
        ppa_unregister_client(ppa_client_);
    release_rotation_buffers_();
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
    auto*      self  = static_cast<MipiDsiDisplay*>(user_ctx);
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
    BaseType_t woken               = pdFALSE;
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
    dpi_config.num_fbs                        = 2;
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
    if (esp_lcd_dpi_panel_get_frame_buffer(panel_, 2, reinterpret_cast<void**>(&dpi_frame_buffers_[0]), reinterpret_cast<void**>(&dpi_frame_buffers_[1])) != ESP_OK || dpi_frame_buffers_[0] == nullptr || dpi_frame_buffers_[1] == nullptr)
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
    refresh_done_     = xSemaphoreCreateBinary();
    if (color_trans_done_ == nullptr || refresh_done_ == nullptr)
        return false;
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
    callbacks.on_color_trans_done                 = on_color_trans_done_;
    callbacks.on_refresh_done                     = on_refresh_done_;
    if (esp_lcd_dpi_panel_register_event_callbacks(panel_, &callbacks, this) != ESP_OK)
        return false;
    if (esp_lcd_panel_disp_on_off(panel_, true) != ESP_OK)
        ESP_LOGW(TAG, "panel display-on command is not supported; relying on panel init sequence");
    initialized_ = true;
    return true;
}

brick::interfaces::display::DisplaySize MipiDsiDisplay::size() const
{
    return { logical_width_, logical_height_ };
}

brick::interfaces::display::PixelFormat MipiDsiDisplay::pixel_format() const
{
    return brick::interfaces::display::PixelFormat::rgb565;
}

brick::interfaces::display::DisplayCapabilities MipiDsiDisplay::capabilities() const
{
    return {
        brick::interfaces::display::DisplayPanelType::mipi_dsi,
        { logical_width_, logical_height_ },
        pixel_format(),
        static_cast<std::size_t>(config_.width) * config_.height * brick::interfaces::display::pixel_format_bytes(pixel_format()),
        64,
        0,
        true,
        true,
        true,
        true,
        true,
        2,
        brick::interfaces::display::RenderMode::direct,
    };
}

bool MipiDsiDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    const bool old_swapped = rotation_ == brick::interfaces::display::Rotation::rotate_90 || rotation_ == brick::interfaces::display::Rotation::rotate_270;
    const bool new_swapped = rotation == brick::interfaces::display::Rotation::rotate_90 || rotation == brick::interfaces::display::Rotation::rotate_270;
    if (initialized_ && old_swapped != new_swapped)
        return false;
    rotation_       = rotation;
    logical_width_  = new_swapped ? config_.height : config_.width;
    logical_height_ = new_swapped ? config_.width : config_.height;
    return !initialized_ || apply_rotation_();
}

bool MipiDsiDisplay::draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer)
{
    if (!initialized_ || panel_ == nullptr || area.empty() || area.x < 0 || area.y < 0 || area.x + area.width > logical_width_ || area.y + area.height > logical_height_ || !buffer.valid() || buffer.width != static_cast<std::uint32_t>(area.width)
        || buffer.height != static_cast<std::uint32_t>(area.height) || buffer.format != pixel_format() || buffer.stride_bytes != static_cast<std::size_t>(area.width) * 2)
        return false;
    if (rotation_ != brick::interfaces::display::Rotation::rotate_0)
        return rotated_transfer_(area, buffer);
    if (area.x == 0 && area.y == 0 && area.width == logical_width_ && area.height == logical_height_ && dpi_frame_buffers_[0] != nullptr && dpi_frame_buffers_[1] != nullptr)
        return full_frame_transfer_(buffer);
    const auto bytes = buffer.stride_bytes * static_cast<std::size_t>(area.height);
    if (esp_ptr_external_ram(buffer.data) && esp_cache_msync(const_cast<std::uint8_t*>(buffer.data), bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED) != ESP_OK)
        return false;
    const auto err = esp_lcd_panel_draw_bitmap(panel_, area.x, area.y, area.x + area.width, area.y + area.height, buffer.data);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "draw_bitmap area=(%d,%d)-(%d,%d) failed: %s", area.x, area.y, area.x + area.width, area.y + area.height, esp_err_to_name(err));
    transfer_pending_ = err == ESP_OK;
    return err == ESP_OK;
}

bool MipiDsiDisplay::full_frame_transfer_(const brick::interfaces::display::PixelBuffer& buffer)
{
    if (transfer_pending_ && !wait_for_transfer_complete(1000))
        return false;
    const auto next  = static_cast<std::uint8_t>(dpi_active_frame_buffer_ ^ 1U);
    const auto bytes = static_cast<std::size_t>(config_.width) * config_.height * sizeof(std::uint16_t);
    std::memcpy(dpi_frame_buffers_[next], buffer.data, bytes);
    if (esp_cache_msync(dpi_frame_buffers_[next], bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED) != ESP_OK)
        return false;
    const auto err = esp_lcd_panel_draw_bitmap(panel_, 0, 0, config_.width, config_.height, dpi_frame_buffers_[next]);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "full frame page flip failed: %s", esp_err_to_name(err));
    if (err == ESP_OK)
        dpi_active_frame_buffer_ = next;
    transfer_pending_ = err == ESP_OK;
    return err == ESP_OK;
}

bool MipiDsiDisplay::wait_for_transfer_complete(std::uint32_t timeout_ms)
{
    if (color_trans_done_ != nullptr && refresh_done_ != nullptr)
    {
        if (xSemaphoreTake(color_trans_done_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
            return false;
        color_ready_for_refresh_ = true;
        const bool complete      = xSemaphoreTake(refresh_done_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
        if (complete)
            transfer_pending_ = false;
        return complete;
    }

    // esp_lcd's DPI draw call queues the source buffer for scan-out. Keep the
    // LVGL buffer alive for at least one complete video frame before reuse.
    const auto h_total  = static_cast<std::uint32_t>(config_.width) + config_.hsync_back_porch + config_.hsync_front_porch + config_.hsync_pulse_width;
    const auto v_total  = static_cast<std::uint32_t>(config_.height) + config_.vsync_back_porch + config_.vsync_front_porch + config_.vsync_pulse_width;
    const auto frame_us = static_cast<std::uint32_t>((static_cast<double>(h_total) * v_total * 1'000'000.0) / (config_.pixel_clock_mhz * 1'000'000.0)) + 2'000;
    const auto wait_ms  = (frame_us + 999) / 1'000;
    if (wait_ms > timeout_ms)
        return false;
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
    transfer_pending_ = false;
    return true;
}

bool MipiDsiDisplay::ensure_rotation_buffers_()
{
    if (rotation_source_ != nullptr && rotation_target_ != nullptr)
        return true;
    const auto source_bytes = static_cast<std::size_t>(logical_width_) * logical_height_ * sizeof(std::uint16_t);
    const auto target_bytes = static_cast<std::size_t>(config_.width) * config_.height * sizeof(std::uint16_t);
    rotation_source_        = static_cast<std::uint16_t*>(heap_caps_aligned_alloc(64, source_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    rotation_target_        = static_cast<std::uint16_t*>(heap_caps_aligned_alloc(64, target_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (rotation_source_ == nullptr || rotation_target_ == nullptr)
    {
        release_rotation_buffers_();
        ESP_LOGE(TAG, "rotation requires PSRAM buffers (%zu + %zu bytes)", source_bytes, target_bytes);
        return false;
    }
    memset(rotation_source_, 0, source_bytes);
    memset(rotation_target_, 0, target_bytes);
    if (ppa_client_ == nullptr)
    {
        const ppa_client_config_t ppa_config{ .oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1, .data_burst_length = PPA_DATA_BURST_LENGTH_128 };
        if (ppa_register_client(&ppa_config, &ppa_client_) != ESP_OK)
        {
            ppa_client_ = nullptr;
            ESP_LOGW(TAG, "PPA rotation unavailable; using CPU fallback");
        }
    }
    return true;
}

void MipiDsiDisplay::release_rotation_buffers_()
{
    if (rotation_source_ != nullptr)
        heap_caps_free(rotation_source_);
    if (rotation_target_ != nullptr)
        heap_caps_free(rotation_target_);
    rotation_source_ = nullptr;
    rotation_target_ = nullptr;
}

bool MipiDsiDisplay::ppa_rotate_buffer_(const std::uint16_t* source)
{
    if (ppa_client_ == nullptr)
        return false;
    ppa_srm_oper_config_t operation{};
    operation.in.buffer       = source;
    operation.in.pic_w        = logical_width_;
    operation.in.pic_h        = logical_height_;
    operation.in.block_w      = logical_width_;
    operation.in.block_h      = logical_height_;
    operation.in.srm_cm       = PPA_SRM_COLOR_MODE_RGB565;
    operation.out.buffer      = rotation_target_;
    operation.out.buffer_size = static_cast<uint32_t>(config_.width) * config_.height * sizeof(std::uint16_t);
    operation.out.pic_w       = config_.width;
    operation.out.pic_h       = config_.height;
    operation.out.srm_cm      = PPA_SRM_COLOR_MODE_RGB565;
    switch (rotation_)
    {
        case brick::interfaces::display::Rotation::rotate_90:
            operation.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
            break;
        case brick::interfaces::display::Rotation::rotate_180:
            operation.rotation_angle = PPA_SRM_ROTATION_ANGLE_180;
            break;
        default:
            operation.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
            break;
    }
    operation.scale_x = 1.0f;
    operation.scale_y = 1.0f;
    operation.mode    = PPA_TRANS_MODE_BLOCKING;
    return ppa_do_scale_rotate_mirror(ppa_client_, &operation) == ESP_OK;
}

void MipiDsiDisplay::rotate_buffer_(const std::uint16_t* source)
{
    const auto pw = static_cast<std::size_t>(config_.width);
    const auto lw = static_cast<std::size_t>(logical_width_);
    const auto lh = static_cast<std::size_t>(logical_height_);
    if (rotation_ == brick::interfaces::display::Rotation::rotate_90)
    {
        for (std::size_t x = 0; x < lw; ++x)
            for (std::size_t y = 0; y < lh; ++y)
                rotation_target_[x * pw + (pw - 1 - y)] = source[y * lw + x];
        return;
    }
    if (rotation_ == brick::interfaces::display::Rotation::rotate_270)
    {
        for (std::size_t x = 0; x < lw; ++x)
            for (std::size_t y = 0; y < lh; ++y)
                rotation_target_[(lw - 1 - x) * pw + y] = source[y * lw + x];
        return;
    }
    for (std::size_t y = 0; y < lh; ++y)
        for (std::size_t x = 0; x < lw; ++x)
        {
            std::size_t dst_x;
            std::size_t dst_y;
            dst_x                                = pw - 1 - x;
            dst_y                                = lh - 1 - y;
            rotation_target_[dst_y * pw + dst_x] = source[y * lw + x];
        }
}

bool MipiDsiDisplay::rotated_transfer_(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer)
{
    if (!ensure_rotation_buffers_())
        return false;
    if (transfer_pending_ && !wait_for_transfer_complete(1000))
        return false;
    const auto* src        = reinterpret_cast<const std::uint16_t*>(buffer.data);
    const bool  full_frame = area.x == 0 && area.y == 0 && area.width == logical_width_ && area.height == logical_height_;
    if (!full_frame)
    {
        auto* dst = rotation_source_ + static_cast<std::size_t>(area.y) * logical_width_ + area.x;
        for (std::int16_t row = 0; row < area.height; ++row)
            memcpy(dst + static_cast<std::size_t>(row) * logical_width_, src + static_cast<std::size_t>(row) * area.width, static_cast<std::size_t>(area.width) * sizeof(std::uint16_t));
        src = rotation_source_;
    }
    if (!full_frame || !ppa_rotate_buffer_(src))
        rotate_buffer_(src);
    const auto bytes = static_cast<std::size_t>(config_.width) * config_.height * sizeof(std::uint16_t);
    if (esp_cache_msync(rotation_target_, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED) != ESP_OK)
        return false;
    const auto err    = esp_lcd_panel_draw_bitmap(panel_, 0, 0, config_.width, config_.height, rotation_target_);
    transfer_pending_ = err == ESP_OK;
    return err == ESP_OK;
}

bool MipiDsiDisplay::send_init_sequence_()
{
    std::size_t index            = 0;
    bool        sleep_out_waited = false;
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
    // DPI panels do not implement swap_xy. Keep the physical scanout geometry
    // and rotate the logical framebuffer in BRICK for every non-zero rotation.
    if (swap || mirror_x || mirror_y)
        return ensure_rotation_buffers_();
    return true;
}

}  // namespace brick::platform::esp32::p4
