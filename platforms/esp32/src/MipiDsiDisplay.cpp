#include "brick/platform/esp32/p4/MipiDsiDisplay.h"

#include "brick/core/display/FrameBufferCopy.h"
#include "esp_cache.h"
#include "sdkconfig.h"
#include <cstring>

namespace brick::platform::esp32::p4
{

using brick::interfaces::display::DisplayRect;
using brick::interfaces::display::PixelBuffer;
using brick::interfaces::display::Rotation;

namespace
{
    constexpr std::uint32_t kFlipTimeoutMs = 1000;
    /// A whole 800x1280 frame takes the PPA about 35 ms.
    constexpr std::uint32_t kPpaTimeoutMs = 200;
    // DIG-734: the PPA cuts a block into units (64 output pixels wide, 16
    // input rows high), and when there are several and the last one is small
    // the 2D-DMA can miss it: the PPA never finishes unless it bypasses the
    // macro block order. ESP-IDF up to 5.5.5 decides that from the unturned
    // block and only for a last unit below its FIFO (12 x 128 bits). Larger
    // ones stop it too: on a JC8012 (rev 1.3) 198x256 and 198x80 RGB565 (last
    // units of 6 x 16 pixels, 1536 bits) and 135x127 (7 x 15, 1680 bits) did
    // for good. Up to 16 x 128 bits counts as small here.
    constexpr std::uint32_t kUnitWidth   = 64;
    constexpr std::uint32_t kUnitRows    = 16;
    constexpr std::uint32_t kFifoBits    = 12 * 128;
    constexpr std::uint32_t kSmallBits   = 16 * 128;
    constexpr std::uint32_t kPixelBits   = 16;
    constexpr std::int32_t  kMaxBandRows = 64;

    std::uint32_t last_unit(std::uint32_t size, std::uint32_t unit)
    {
        const std::uint32_t rest = size % unit;
        return rest == 0 ? unit : rest;
    }

    bool several_units(std::uint32_t out_width, std::uint32_t in_rows)
    {
        return out_width > kUnitWidth || in_rows > kUnitRows;
    }

    std::uint32_t last_unit_bits(std::uint32_t out_width, std::uint32_t in_rows)
    {
        return last_unit(out_width, kUnitWidth) * last_unit(in_rows, kUnitRows) * kPixelBits;
    }

    /// ESP-IDF's decision for a width x rows input block.
    bool idf_bypasses(std::uint32_t width, std::uint32_t rows)
    {
        return several_units(width, rows) && last_unit_bits(width, rows) < kFifoBits;
    }

    /// What the PPA needs for a block out_width pixels wide when it leaves.
    bool ppa_needs_bypass(std::uint32_t out_width, std::uint32_t in_rows)
    {
        return several_units(out_width, in_rows) && last_unit_bits(out_width, in_rows) <= kSmallBits;
    }

    /// A width x rows block the PPA finishes: ESP-IDF decides what it needs.
    /// Turned by 90/270 degrees the block leaves the PPA rows wide and width
    /// high.
    bool safe_block(std::uint32_t width, std::uint32_t rows, bool turned)
    {
        return idf_bypasses(width, rows) == (turned ? ppa_needs_bypass(rows, width) : ppa_needs_bypass(width, rows));
    }

    /// Rows per band: all of them when the area is safe, else the most rows
    /// (fewer than height, at most kMaxBandRows turned) whose bands and last
    /// band are all safe; 0 when there is none.
    std::int32_t safe_band_rows(std::int32_t width, std::int32_t height, bool turned)
    {
        const auto w = static_cast<std::uint32_t>(width);
        if (safe_block(w, static_cast<std::uint32_t>(height), turned))
            return height;
        const std::int32_t most = turned && height > kMaxBandRows ? kMaxBandRows : height - 1;
        for (std::int32_t rows = most; rows > 0; --rows)
        {
            const std::int32_t rest = height % rows;
            if (safe_block(w, static_cast<std::uint32_t>(rows), turned)
                && (rest == 0 || safe_block(w, static_cast<std::uint32_t>(rest), turned)))
                return rows;
        }
        return 0;
    }
    constexpr std::size_t kBytesPerPixel = 2;
    // PSRAM is behind the L2 cache, whose lines are at least as long as L1's.
#if defined(CONFIG_CACHE_L2_CACHE_LINE_SIZE)
    constexpr std::size_t kCacheLineBytes = CONFIG_CACHE_L2_CACHE_LINE_SIZE;
#else
    constexpr std::size_t kCacheLineBytes = 128;
#endif

    bool swaps_axes(Rotation rotation)
    {
        return rotation == Rotation::rotate_90 || rotation == Rotation::rotate_270;
    }

    /// The PPA turns counter-clockwise; the logical image turns clockwise.
    ppa_srm_rotation_angle_t ppa_angle(Rotation rotation)
    {
        switch (rotation)
        {
            case Rotation::rotate_90:
                return PPA_SRM_ROTATION_ANGLE_270;
            case Rotation::rotate_180:
                return PPA_SRM_ROTATION_ANGLE_180;
            case Rotation::rotate_270:
                return PPA_SRM_ROTATION_ANGLE_90;
            default:
                return PPA_SRM_ROTATION_ANGLE_0;
        }
    }

    /// RGB565 widened to the PPA fill color; the PPA truncates it back.
    color_pixel_argb8888_data_t fill_color(std::uint16_t color)
    {
        const std::uint32_t         red   = (color >> 11) & 0x1FU;
        const std::uint32_t         green = (color >> 5) & 0x3FU;
        const std::uint32_t         blue  = color & 0x1FU;
        color_pixel_argb8888_data_t result{};
        result.r = (red << 3) | (red >> 2);
        result.g = (green << 2) | (green >> 4);
        result.b = (blue << 3) | (blue >> 2);
        result.a = 0xFF;
        return result;
    }
}  // namespace

MipiDsiDisplay::MipiDsiDisplay(MipiDsiPanelConfig config) : config_(config), rotation_(config.default_rotation)
{
    const bool swapped = swaps_axes(rotation_);
    logical_width_     = swapped ? config_.height : config_.width;
    logical_height_    = swapped ? config_.width : config_.height;
}

MipiDsiDisplay::~MipiDsiDisplay()
{
    if (ppa_srm_ != nullptr)
        ppa_unregister_client(ppa_srm_);
    if (ppa_fill_ != nullptr)
        ppa_unregister_client(ppa_fill_);
    if (color_trans_done_ != nullptr)
        vSemaphoreDelete(color_trans_done_);
    if (refresh_done_ != nullptr)
        vSemaphoreDelete(refresh_done_);
    if (ppa_done_ != nullptr)
        vSemaphoreDelete(ppa_done_);
    if (panel_ != nullptr)
        esp_lcd_panel_del(panel_);
    if (io_ != nullptr)
        esp_lcd_panel_io_del(io_);
    if (bus_ != nullptr)
        esp_lcd_del_dsi_bus(bus_);
    if (mipi_dsi_ldo_ != nullptr)
        esp_ldo_release_channel(mipi_dsi_ldo_);
}

bool IRAM_ATTR MipiDsiDisplay::on_color_trans_done_(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*,
                                                    void* user_ctx)
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
    // Only a refresh the task waits for counts; reading and clearing the
    // flag is one atomic step.
    if (!self->frame_end_wanted_.exchange(false))
        return false;
    BaseType_t woken = pdFALSE;
    if (self->refresh_done_ != nullptr)
        xSemaphoreGiveFromISR(self->refresh_done_, &woken);
    return woken == pdTRUE;
}

bool IRAM_ATTR MipiDsiDisplay::on_ppa_done_(ppa_client_handle_t, ppa_event_data_t*, void* user_data)
{
    auto*      self  = static_cast<MipiDsiDisplay*>(user_data);
    BaseType_t woken = pdFALSE;
    if (self != nullptr && self->ppa_done_ != nullptr)
        xSemaphoreGiveFromISR(self->ppa_done_, &woken);
    return woken == pdTRUE;
}

Status MipiDsiDisplay::begin()
{
    if (initialized_)
        return Status::ok;
    if (config_.width == 0 || config_.height == 0)
        return Status::invalid_argument;
    if (const Status status = create_bus_(); !is_ok(status))
        return status;
    if (const Status status = create_dpi_panel_(); !is_ok(status))
        return status;
    reset_panel_();
    if (const esp_err_t err = esp_lcd_panel_init(panel_); err != ESP_OK)
        return to_status(err);
    if (const Status status = send_init_sequence_(); !is_ok(status))
        return status;
    if (const Status status = register_callbacks_(); !is_ok(status))
        return status;
    create_ppa_clients_();
    if (esp_lcd_panel_disp_on_off(panel_, true) != ESP_OK)
        ESP_LOGW(TAG, "panel display-on command is not supported; relying on panel init sequence");
    initialized_ = true;
    return Status::ok;
}

Status MipiDsiDisplay::create_bus_()
{
    if (config_.mipi_dsi_ldo_channel >= 0 && config_.mipi_dsi_ldo_voltage_mv > 0)
    {
        esp_ldo_channel_config_t ldo_config = { .chan_id    = config_.mipi_dsi_ldo_channel,
                                                .voltage_mv = config_.mipi_dsi_ldo_voltage_mv };
        if (const esp_err_t err = esp_ldo_acquire_channel(&ldo_config, &mipi_dsi_ldo_); err != ESP_OK)
            return to_status(err);
    }
    esp_lcd_dsi_bus_config_t bus_config = {};
    bus_config.bus_id                   = 0;
    bus_config.num_data_lanes           = config_.data_lanes;
    bus_config.phy_clk_src              = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_config.lane_bit_rate_mbps       = config_.lane_bit_rate_mbps;
    if (const esp_err_t err = esp_lcd_new_dsi_bus(&bus_config, &bus_); err != ESP_OK)
        return to_status(err);
    // Panel commands and their parameters are 8 bit.
    esp_lcd_dbi_io_config_t io_config = {};
    io_config.virtual_channel         = 0;
    io_config.lcd_cmd_bits            = 8;
    io_config.lcd_param_bits          = 8;
    return to_status(esp_lcd_new_panel_io_dbi(bus_, &io_config, &io_));
}

Status MipiDsiDisplay::create_dpi_panel_()
{
    esp_lcd_dpi_panel_config_t dpi_config     = {};
    dpi_config.virtual_channel                = 0;
    dpi_config.dpi_clk_src                    = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_config.dpi_clock_freq_mhz             = config_.pixel_clock_mhz;
    dpi_config.pixel_format                   = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi_config.in_color_format                = LCD_COLOR_FMT_RGB565;
    dpi_config.out_color_format               = LCD_COLOR_FMT_RGB565;
    dpi_config.num_fbs                        = kDpiFrameBufferCount;
    dpi_config.flags.use_dma2d                = true;
    dpi_config.video_timing.h_size            = config_.width;
    dpi_config.video_timing.v_size            = config_.height;
    dpi_config.video_timing.hsync_pulse_width = config_.hsync_pulse_width;
    dpi_config.video_timing.hsync_back_porch  = config_.hsync_back_porch;
    dpi_config.video_timing.hsync_front_porch = config_.hsync_front_porch;
    dpi_config.video_timing.vsync_pulse_width = config_.vsync_pulse_width;
    dpi_config.video_timing.vsync_back_porch  = config_.vsync_back_porch;
    dpi_config.video_timing.vsync_front_porch = config_.vsync_front_porch;
    if (const esp_err_t err = esp_lcd_new_panel_dpi(bus_, &dpi_config, &panel_); err != ESP_OK)
        return to_status(err);
    if (esp_lcd_dpi_panel_get_frame_buffer(panel_, kDpiFrameBufferCount,
                                           reinterpret_cast<void**>(&dpi_frame_buffers_[0]),
                                           reinterpret_cast<void**>(&dpi_frame_buffers_[1]))
            != ESP_OK
        || dpi_frame_buffers_[0] == nullptr || dpi_frame_buffers_[1] == nullptr)
        return Status::error;
    return Status::ok;
}

void MipiDsiDisplay::create_ppa_clients_()
{
    // The PPA writes whole cache lines; esp_lcd allocates the frame buffers
    // cache-line aligned and rounds their size up to a whole line.
    ppa_out_bytes_ = (frame_buffer_bytes_() + kCacheLineBytes - 1) / kCacheLineBytes * kCacheLineBytes;

    ppa_client_config_t ppa_config{};
    ppa_config.oper_type = PPA_OPERATION_SRM;
    // Copies queue up, so the copies of a frame run while LVGL draws on.
    ppa_config.max_pending_trans_num = kPpaQueueDepth;
    ppa_config.data_burst_length     = PPA_DATA_BURST_LENGTH_128;
    ppa_done_                        = xSemaphoreCreateCounting(kPpaQueueDepth, 0);
    ppa_event_callbacks_t callbacks{};
    callbacks.on_trans_done = on_ppa_done_;
    if (ppa_done_ == nullptr || ppa_register_client(&ppa_config, &ppa_srm_) != ESP_OK
        || ppa_client_register_event_callbacks(ppa_srm_, &callbacks) != ESP_OK)
    {
        if (ppa_srm_ != nullptr)
            ppa_unregister_client(ppa_srm_);
        ppa_srm_ = nullptr;
        ESP_LOGW(TAG, "PPA copy and rotation unavailable; the CPU copies instead");
    }
    ppa_config.oper_type             = PPA_OPERATION_FILL;
    ppa_config.max_pending_trans_num = 1;
    if (ppa_register_client(&ppa_config, &ppa_fill_) != ESP_OK)
        ppa_fill_ = nullptr;
}

void MipiDsiDisplay::reset_panel_()
{
    if (config_.reset_gpio == GPIO_NUM_NC)
        return;
    gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(config_.reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(kResetPulseMs));
    gpio_set_level(config_.reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(kResetPulseMs));
    gpio_set_level(config_.reset_gpio, 1);
}

Status MipiDsiDisplay::register_callbacks_()
{
    color_trans_done_ = xSemaphoreCreateBinary();
    refresh_done_     = xSemaphoreCreateBinary();
    if (color_trans_done_ == nullptr || refresh_done_ == nullptr)
        return Status::no_memory;
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
    callbacks.on_color_trans_done                 = on_color_trans_done_;
    // ESP-IDF 5.5 renamed on_refresh_done (same union member and signature).
    callbacks.on_frame_buf_complete = on_refresh_done_;
    return to_status(esp_lcd_dpi_panel_register_event_callbacks(panel_, &callbacks, this));
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
        frame_buffer_bytes_(),
        // Buffers the PPA or the cache sync touch must cover whole cache lines.
        kCacheLineBytes,
        0,
        true,
        true,
        true,
        true,
        true,
        kDpiFrameBufferCount,
        brick::interfaces::display::RenderMode::direct,
    };
}

Status MipiDsiDisplay::set_rotation(Rotation rotation)
{
    // The DPI scan-out geometry is fixed once the panel is running.
    if (initialized_ && swaps_axes(rotation_) != swaps_axes(rotation))
        return Status::not_supported;
    if (initialized_)
        if (const Status status = wait_for_transfer_complete(kFlipTimeoutMs); !is_ok(status))
            return status;
    rotation_          = rotation;
    const bool swapped = swaps_axes(rotation);
    logical_width_     = swapped ? config_.height : config_.width;
    logical_height_    = swapped ? config_.width : config_.height;
    return Status::ok;
}

bool MipiDsiDisplay::valid_area_(DisplayRect area) const
{
    return !area.empty() && area.x >= 0 && area.y >= 0 && area.x + area.width <= logical_width_
           && area.y + area.height <= logical_height_;
}

DisplayRect MipiDsiDisplay::to_physical_(DisplayRect area) const
{
    const auto panel_width  = static_cast<std::int32_t>(config_.width);
    const auto panel_height = static_cast<std::int32_t>(config_.height);
    switch (rotation_)
    {
        case Rotation::rotate_90:
            return { panel_width - area.y - area.height, area.x, area.height, area.width };
        case Rotation::rotate_180:
            return { panel_width - area.x - area.width, panel_height - area.y - area.height, area.width, area.height };
        case Rotation::rotate_270:
            return { area.y, panel_height - area.x - area.width, area.height, area.width };
        default:
            return area;
    }
}

Status MipiDsiDisplay::draw_buffer(DisplayRect area, const PixelBuffer& buffer)
{
    if (!initialized_ || panel_ == nullptr)
        return Status::not_ready;
    if (!valid_area_(area) || !buffer.valid() || buffer.width != static_cast<std::uint32_t>(area.width)
        || buffer.height != static_cast<std::uint32_t>(area.height) || buffer.format != pixel_format()
        || buffer.stride_bytes != static_cast<std::size_t>(area.width) * kBytesPerPixel)
        return Status::invalid_argument;
    // The 2D-DMA takes one operation at a time, and a flip has to reach the
    // screen before the other frame buffer is written.
    if (const Status status = wait_for_transfer_complete(kFlipTimeoutMs); !is_ok(status))
        return status;
    if (area.x == 0 && area.y == 0 && area.width == logical_width_ && area.height == logical_height_)
        return full_frame_transfer_(buffer);
    return partial_transfer_(area, buffer);
}

Status MipiDsiDisplay::full_frame_transfer_(const PixelBuffer& buffer)
{
    // A frame drawn into a frame buffer is shown as it is.
    if (rotation_ == Rotation::rotate_0)
        for (std::uint8_t index = 0; index < kDpiFrameBufferCount; ++index)
            if (buffer.data == dpi_frame_buffers_[index])
                return flip_to_(index);
    // Otherwise it is written into the hidden frame buffer and flipped in.
    const auto        hidden = static_cast<std::uint8_t>(dpi_active_frame_buffer_ ^ 1U);
    const DisplayRect frame{ 0, 0, logical_width_, logical_height_ };
    if (const Status status = write_area_(frame, buffer.data, hidden); !is_ok(status))
        return status;
    return flip_to_(hidden);
}

Status MipiDsiDisplay::partial_transfer_(DisplayRect area, const PixelBuffer& buffer)
{
    if (rotation_ != Rotation::rotate_0)
        return write_area_(area, buffer.data, dpi_active_frame_buffer_);
    // esp_lcd copies the area with the 2D-DMA (it also writes the caller's
    // pixels back from the cache) and reports the end in on_color_trans_done.
    (void)xSemaphoreTake(color_trans_done_, 0);
    const esp_err_t err =
        esp_lcd_panel_draw_bitmap(panel_, area.x, area.y, area.x + area.width, area.y + area.height, buffer.data);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "draw_bitmap area=(%d,%d)-(%d,%d) failed: %s", static_cast<int>(area.x), static_cast<int>(area.y),
                 static_cast<int>(area.x + area.width), static_cast<int>(area.y + area.height), esp_err_to_name(err));
        return to_status(err);
    }
    pending_ = Pending::copy;
    return Status::ok;
}

Status MipiDsiDisplay::write_area_(DisplayRect area, const std::uint8_t* pixels, std::uint8_t frame_buffer)
{
    if (ppa_srm_ != nullptr && is_ok(ppa_write_bands_(area, pixels, frame_buffer)))
        return Status::ok;
    cpu_write_area_(area, reinterpret_cast<const std::uint16_t*>(pixels),
                    reinterpret_cast<std::uint16_t*>(dpi_frame_buffers_[frame_buffer]));
    return sync_rows_(frame_buffer, area);
}

Status MipiDsiDisplay::ppa_write_bands_(DisplayRect area, const std::uint8_t* pixels, std::uint8_t frame_buffer,
                                        bool wait)
{
    // ESP-IDF up to 5.5.5 gets the DIG-734 workaround wrong for some blocks
    // (esp-idf #19023 for 90/270-degree turns), and the PPA then never
    // finishes them. The area goes in bands it gets right; an area without
    // such bands the CPU copies.
    const std::int32_t band_rows = safe_band_rows(area.width, area.height, swaps_axes(rotation_));
    if (band_rows == 0)
        return Status::not_supported;
    const std::size_t row_bytes = static_cast<std::size_t>(area.width) * kBytesPerPixel;
    for (std::int32_t top = 0; top < area.height; top += band_rows)
    {
        const std::int32_t rows = area.height - top < band_rows ? area.height - top : band_rows;
        const DisplayRect  band{ area.x, area.y + top, area.width, rows };
        if (const Status status =
                ppa_write_area_(band, pixels + static_cast<std::size_t>(top) * row_bytes, frame_buffer, wait);
            !is_ok(status))
            return status;
    }
    return Status::ok;
}

Status MipiDsiDisplay::ppa_write_area_(DisplayRect area, const std::uint8_t* pixels, std::uint8_t frame_buffer,
                                       bool wait)
{
    // One PPA scale-rotate-mirror pass at scale 1: the 2D-DMA copies (and
    // rotates) the area straight into place while this task sleeps. The PPA
    // writes the caller's pixels back from the cache and drops stale cache
    // lines of the destination itself.
    const DisplayRect     physical = to_physical_(area);
    ppa_srm_oper_config_t operation{};
    operation.in.buffer          = pixels;
    operation.in.pic_w           = static_cast<std::uint32_t>(area.width);
    operation.in.pic_h           = static_cast<std::uint32_t>(area.height);
    operation.in.block_w         = static_cast<std::uint32_t>(area.width);
    operation.in.block_h         = static_cast<std::uint32_t>(area.height);
    operation.out.buffer         = dpi_frame_buffers_[frame_buffer];
    operation.out.block_offset_x = static_cast<std::uint32_t>(physical.x);
    operation.out.block_offset_y = static_cast<std::uint32_t>(physical.y);
    operation.rotation_angle     = ppa_angle(rotation_);
    return run_srm_(operation, area, wait);
}

Status MipiDsiDisplay::run_srm_(ppa_srm_oper_config_t& operation, DisplayRect area, bool wait)
{
    // Some areas the PPA never finishes (esp-idf #19023, #18999), and a
    // blocking call would wait forever. The task waits with a timeout
    // instead; until the late copy ends the CPU copies.
    if (const Status status = settle_late_srm_(); !is_ok(status))
        return status;
    // A full queue makes room by waiting for its oldest copy.
    if (srm_queued_ >= kPpaQueueDepth)
        if (const Status status = take_srm_done_(); !is_ok(status))
            return status;
    operation.in.srm_cm       = PPA_SRM_COLOR_MODE_RGB565;
    operation.out.buffer_size = static_cast<std::uint32_t>(ppa_out_bytes_);
    operation.out.pic_w       = config_.width;
    operation.out.pic_h       = config_.height;
    operation.out.srm_cm      = PPA_SRM_COLOR_MODE_RGB565;
    operation.scale_x         = 1.0f;
    operation.scale_y         = 1.0f;
    operation.mode            = PPA_TRANS_MODE_NON_BLOCKING;
    operation.user_data       = this;
    if (const esp_err_t err = ppa_do_scale_rotate_mirror(ppa_srm_, &operation); err != ESP_OK)
        return to_status(err);
    // What each queued copy is, for the log of one that does not end.
    SrmCopy& copy = srm_copies_[(srm_oldest_ + srm_queued_) % kPpaQueueDepth];
    copy.area     = area;
    copy.in =
        reinterpret_cast<std::uintptr_t>(operation.in.buffer)
        + (static_cast<std::uintptr_t>(operation.in.block_offset_y) * operation.in.pic_w + operation.in.block_offset_x)
              * kBytesPerPixel;
    copy.out = reinterpret_cast<std::uintptr_t>(operation.out.buffer)
               + (static_cast<std::uintptr_t>(operation.out.block_offset_y) * operation.out.pic_w
                  + operation.out.block_offset_x)
                     * kBytesPerPixel;
    ++srm_queued_;
    pending_ = Pending::ppa;
    return wait ? wait_for_srm_() : Status::ok;
}

Status MipiDsiDisplay::wait_for_srm_()
{
    // Every queued copy, oldest first.
    while (srm_queued_ > 0)
        if (const Status status = take_srm_done_(); !is_ok(status))
            return status;
    if (pending_ == Pending::ppa)
        pending_ = Pending::none;
    return Status::ok;
}

Status MipiDsiDisplay::take_srm_done_()
{
    // The end of the oldest queued copy.
    if (xSemaphoreTake(ppa_done_, pdMS_TO_TICKS(kPpaTimeoutMs)) == pdTRUE)
    {
        srm_oldest_ = static_cast<std::uint8_t>((srm_oldest_ + 1U) % kPpaQueueDepth);
        --srm_queued_;
        return Status::ok;
    }
    srm_late_            = true;
    pending_             = Pending::none;
    const SrmCopy& stuck = srm_copies_[srm_oldest_];
    ESP_LOGW(TAG,
             "PPA copy of %dx%d at (%d,%d), rotation %d, from 0x%08x to 0x%08x did not end (%u queued); the CPU "
             "copies until it does",
             static_cast<int>(stuck.area.width), static_cast<int>(stuck.area.height), static_cast<int>(stuck.area.x),
             static_cast<int>(stuck.area.y), static_cast<int>(rotation_), static_cast<unsigned>(stuck.in),
             static_cast<unsigned>(stuck.out), static_cast<unsigned>(srm_queued_));
    return Status::timeout;
}

Status MipiDsiDisplay::settle_late_srm_()
{
    // Counts the copies that have ended since; busy while some have not.
    if (!srm_late_)
        return Status::ok;
    while (srm_queued_ > 0 && xSemaphoreTake(ppa_done_, 0) == pdTRUE)
    {
        srm_oldest_ = static_cast<std::uint8_t>((srm_oldest_ + 1U) % kPpaQueueDepth);
        --srm_queued_;
    }
    if (srm_queued_ > 0)
        return Status::busy;
    srm_late_ = false;
    ESP_LOGI(TAG, "the late PPA copies have ended; the PPA copies again");
    return Status::ok;
}

Status MipiDsiDisplay::copy_to_frame_buffer(std::uint8_t index, DisplayRect area, const PixelBuffer& pixels)
{
    brick::interfaces::display::WritablePixelBuffer frame;
    if (const Status status = get_frame_buffer(index, frame); !is_ok(status))
        return status;
    if (!brick::core::display::area_inside(area, frame.width, frame.height) || !pixels.valid()
        || pixels.width != static_cast<std::uint32_t>(area.width)
        || pixels.height != static_cast<std::uint32_t>(area.height) || pixels.format != pixel_format())
        return Status::invalid_argument;
    // Queued behind earlier PPA copies, in order; anything else ends first.
    if (pending_ != Pending::ppa)
        if (const Status status = wait_for_transfer_complete(kFlipTimeoutMs); !is_ok(status))
            return status;
    // Submitted only: the caller's pixels are read until the copy ends.
    if (ppa_srm_ != nullptr && pixels.stride_bytes == static_cast<std::size_t>(area.width) * kBytesPerPixel
        && is_ok(ppa_write_bands_(area, pixels.data, index, false)))
        return Status::ok;
    // The CPU writes after the queued copies.
    if (const Status status = wait_for_transfer_complete(kFlipTimeoutMs); !is_ok(status))
        return status;
    if (const Status status = brick::core::display::copy_into_frame(frame, area, pixels); !is_ok(status))
        return status;
    return sync_rows_(index, area);
}

Status MipiDsiDisplay::copy_between_frame_buffers(std::uint8_t from, std::uint8_t to, DisplayRect area)
{
    brick::interfaces::display::WritablePixelBuffer source;
    brick::interfaces::display::WritablePixelBuffer target;
    if (const Status status = get_frame_buffer(from, source); !is_ok(status))
        return status;
    if (const Status status = get_frame_buffer(to, target); !is_ok(status))
        return status;
    if (!brick::core::display::area_inside(area, source.width, source.height))
        return Status::invalid_argument;
    if (pending_ != Pending::ppa)
        if (const Status status = wait_for_transfer_complete(kFlipTimeoutMs); !is_ok(status))
            return status;
    if (ppa_srm_ != nullptr && is_ok(ppa_copy_between_(from, to, area)))
        return Status::ok;
    if (const Status status = wait_for_transfer_complete(kFlipTimeoutMs); !is_ok(status))
        return status;
    if (const Status status = brick::core::display::copy_frame_area(source, target, area); !is_ok(status))
        return status;
    return sync_rows_(to, area);
}

Status MipiDsiDisplay::ppa_copy_between_(std::uint8_t from, std::uint8_t to, DisplayRect area)
{
    // In bands the PPA finishes (see ppa_write_bands_()), queued only.
    const std::int32_t band_rows = safe_band_rows(area.width, area.height, false);
    if (band_rows == 0)
        return Status::not_supported;
    for (std::int32_t top = 0; top < area.height; top += band_rows)
    {
        const std::int32_t    rows = area.height - top < band_rows ? area.height - top : band_rows;
        const DisplayRect     band{ area.x, area.y + top, area.width, rows };
        ppa_srm_oper_config_t operation{};
        operation.in.buffer          = dpi_frame_buffers_[from];
        operation.in.pic_w           = config_.width;
        operation.in.pic_h           = config_.height;
        operation.in.block_w         = static_cast<std::uint32_t>(band.width);
        operation.in.block_h         = static_cast<std::uint32_t>(band.height);
        operation.in.block_offset_x  = static_cast<std::uint32_t>(band.x);
        operation.in.block_offset_y  = static_cast<std::uint32_t>(band.y);
        operation.out.buffer         = dpi_frame_buffers_[to];
        operation.out.block_offset_x = static_cast<std::uint32_t>(band.x);
        operation.out.block_offset_y = static_cast<std::uint32_t>(band.y);
        operation.rotation_angle     = PPA_SRM_ROTATION_ANGLE_0;
        if (const Status status = run_srm_(operation, band, false); !is_ok(status))
            return status;
    }
    return Status::ok;
}

Status MipiDsiDisplay::sync_rows_(std::uint8_t frame_buffer, DisplayRect area)
{
    // The scan-out reads PSRAM, not the cache.
    const DisplayRect physical  = to_physical_(area);
    const std::size_t row_bytes = static_cast<std::size_t>(config_.width) * kBytesPerPixel;
    return to_status(
        esp_cache_msync(dpi_frame_buffers_[frame_buffer] + static_cast<std::size_t>(physical.y) * row_bytes,
                        static_cast<std::size_t>(physical.height) * row_bytes,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED));
}

void MipiDsiDisplay::cpu_write_area_(DisplayRect area, const std::uint16_t* pixels, std::uint16_t* frame_buffer) const
{
    const auto panel_width = static_cast<std::size_t>(config_.width);
    const auto width       = static_cast<std::size_t>(area.width);
    const auto height      = static_cast<std::size_t>(area.height);
    const auto left        = static_cast<std::size_t>(area.x);
    const auto top         = static_cast<std::size_t>(area.y);
    const auto last_column = panel_width - 1;
    const auto last_row    = static_cast<std::size_t>(config_.height) - 1;
    for (std::size_t y = 0; y < height; ++y)
    {
        const std::uint16_t* row = pixels + y * width;
        const std::size_t    ly  = top + y;
        switch (rotation_)
        {
            case Rotation::rotate_90:
                for (std::size_t x = 0; x < width; ++x)
                    frame_buffer[(left + x) * panel_width + (last_column - ly)] = row[x];
                break;
            case Rotation::rotate_180:
                for (std::size_t x = 0; x < width; ++x)
                    frame_buffer[(last_row - ly) * panel_width + (last_column - left - x)] = row[x];
                break;
            case Rotation::rotate_270:
                for (std::size_t x = 0; x < width; ++x)
                    frame_buffer[(last_row - left - x) * panel_width + ly] = row[x];
                break;
            default:
                std::memcpy(frame_buffer + ly * panel_width + left, row, width * kBytesPerPixel);
                break;
        }
    }
}

Status MipiDsiDisplay::flip_to_(std::uint8_t frame_buffer)
{
    // With a frame buffer as source esp_lcd only writes it back from the
    // cache, switches the scan-out to it at the end of the current refresh and
    // reports color-transfer-done at once.
    (void)xSemaphoreTake(color_trans_done_, 0);
    const esp_err_t err =
        esp_lcd_panel_draw_bitmap(panel_, 0, 0, config_.width, config_.height, dpi_frame_buffers_[frame_buffer]);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "page flip failed: %s", esp_err_to_name(err));
        return to_status(err);
    }
    dpi_active_frame_buffer_ = frame_buffer;
    pending_                 = Pending::flip;
    return Status::ok;
}

Status MipiDsiDisplay::wait_for_transfer_complete(std::uint32_t timeout_ms)
{
    const Pending pending = pending_;
    if (pending == Pending::none)
        return Status::ok;
    pending_ = Pending::none;
    if (pending == Pending::ppa)
        return wait_for_srm_();
    if (xSemaphoreTake(color_trans_done_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return Status::timeout;
    // The frame buffer shown before a flip is read until the refresh ends.
    return pending == Pending::flip ? wait_frame_end_(timeout_ms) : Status::ok;
}

Status MipiDsiDisplay::wait_for_vsync(std::uint32_t timeout_ms)
{
    if (!initialized_)
        return Status::not_ready;
    // A pending flip completes at the end of a refresh: that is the boundary.
    if (pending_ == Pending::flip)
        return wait_for_transfer_complete(timeout_ms);
    if (const Status status = wait_for_transfer_complete(timeout_ms); !is_ok(status))
        return status;
    return wait_frame_end_(timeout_ms);
}

Status MipiDsiDisplay::wait_frame_end_(std::uint32_t timeout_ms)
{
    // Drop a give left behind by an earlier wait that timed out.
    (void)xSemaphoreTake(refresh_done_, 0);
    frame_end_wanted_ = true;
    if (xSemaphoreTake(refresh_done_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
        return Status::ok;
    frame_end_wanted_ = false;
    return Status::timeout;
}

Status MipiDsiDisplay::fill_rect(DisplayRect area, std::uint16_t color)
{
    if (!initialized_ || panel_ == nullptr)
        return Status::not_ready;
    if (!valid_area_(area))
        return Status::invalid_argument;
    if (ppa_fill_ == nullptr)
        return Status::not_supported;
    if (const Status status = wait_for_transfer_complete(kFlipTimeoutMs); !is_ok(status))
        return status;
    // Into the frame buffer on the screen, like a partial draw.
    const DisplayRect      physical = to_physical_(area);
    ppa_fill_oper_config_t operation{};
    operation.out.buffer         = dpi_frame_buffers_[dpi_active_frame_buffer_];
    operation.out.buffer_size    = static_cast<std::uint32_t>(ppa_out_bytes_);
    operation.out.pic_w          = config_.width;
    operation.out.pic_h          = config_.height;
    operation.out.block_offset_x = static_cast<std::uint32_t>(physical.x);
    operation.out.block_offset_y = static_cast<std::uint32_t>(physical.y);
    operation.out.fill_cm        = PPA_FILL_COLOR_MODE_RGB565;
    operation.fill_block_w       = static_cast<std::uint32_t>(physical.width);
    operation.fill_block_h       = static_cast<std::uint32_t>(physical.height);
    operation.fill_argb_color    = fill_color(color);
    operation.mode               = PPA_TRANS_MODE_BLOCKING;
    return to_status(ppa_do_fill(ppa_fill_, &operation));
}

std::uint8_t MipiDsiDisplay::frame_buffer_count() const
{
    return rotation_ == Rotation::rotate_0 ? kDpiFrameBufferCount : 0;
}

Status MipiDsiDisplay::get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer)
{
    if (!initialized_)
        return Status::not_ready;
    if (index >= frame_buffer_count())
        return Status::invalid_argument;
    buffer = { dpi_frame_buffers_[index],
               config_.width,
               config_.height,
               static_cast<std::size_t>(config_.width) * kBytesPerPixel,
               pixel_format(),
               true };
    return Status::ok;
}

Status MipiDsiDisplay::present_frame_buffer(std::uint8_t index)
{
    if (!initialized_)
        return Status::not_ready;
    if (index >= frame_buffer_count())
        return Status::invalid_argument;
    if (const Status status = wait_for_transfer_complete(kFlipTimeoutMs); !is_ok(status))
        return status;
    return flip_to_(index);
}

std::size_t MipiDsiDisplay::frame_buffer_bytes_() const
{
    return static_cast<std::size_t>(config_.width) * config_.height * kBytesPerPixel;
}

Status MipiDsiDisplay::send_init_sequence_()
{
    std::size_t index            = 0;
    bool        sleep_out_waited = false;
    while (index < config_.init_sequence_size)
    {
        if (config_.init_sequence_size - index < 2)
            return Status::invalid_argument;
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
        if (config_.init_sequence_size - index < count)
            return Status::invalid_argument;
        if (const esp_err_t err = esp_lcd_panel_io_tx_param(io_, command, config_.init_sequence + index, count);
            err != ESP_OK)
            return to_status(err);
        index += count;
        vTaskDelay(pdMS_TO_TICKS(command == 0x11 ? 10 : 1));
    }
    return Status::ok;
}

}  // namespace brick::platform::esp32::p4
