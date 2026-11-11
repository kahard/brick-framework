#include "brick/platform/esp32/s3/St7701sRgbDisplay.h"

#include "brick/core/display/FrameBufferCopy.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace brick::platform::esp32::s3
{

St7701sRgbDisplay::St7701sRgbDisplay(St7701sRgbPanelConfig config) : config_(config)
{
}

St7701sRgbDisplay::~St7701sRgbDisplay()
{
    if (vsync_semaphore_ != nullptr)
        vSemaphoreDelete(vsync_semaphore_);
    if (panel_ != nullptr)
        esp_lcd_panel_del(panel_);
    if (spi_device_ != nullptr)
        spi_bus_remove_device(spi_device_);
    if (spi_ready_)
        spi_bus_free(config_.spi_host);
}

Status St7701sRgbDisplay::begin()
{
    if (initialized_)
        return Status::ok;
    if (config_.width == 0 || config_.height == 0)
        return Status::invalid_argument;

    ESP_LOGI(TAG, "begin ST7701S RGB %ux%u pclk=%luHz", config_.width, config_.height,
             static_cast<unsigned long>(config_.pixel_clock_hz));
    if (const Status status = begin_spi_(); !is_ok(status))
        return status;
    if (const Status status = send_init_sequence_(); !is_ok(status))
        return status;
    compute_frame_timing_();
    if (const Status status = begin_rgb_panel_(); !is_ok(status))
        return status;
    if (config_.backlight_gpio != GPIO_NUM_NC)
    {
        gpio_set_direction(config_.backlight_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config_.backlight_gpio, config_.backlight_active_high ? 1 : 0);
    }
    initialized_ = true;
    return Status::ok;
}

brick::interfaces::display::DisplaySize St7701sRgbDisplay::size() const
{
    return { config_.width, config_.height };
}

brick::interfaces::display::PixelFormat St7701sRgbDisplay::pixel_format() const
{
    return brick::interfaces::display::PixelFormat::rgb565;
}

brick::interfaces::display::DisplayCapabilities St7701sRgbDisplay::capabilities() const
{
    return {
        brick::interfaces::display::DisplayPanelType::rgb,
        { config_.width, config_.height },
        pixel_format(),
        static_cast<std::size_t>(config_.width) * config_.height
            * brick::interfaces::display::pixel_format_bytes(pixel_format()),
        4,
        0,
        true,
        true,
        config_.frame_buffer_count > 1,
        true,
        false,
        config_.frame_buffer_count,
        config_.frame_buffer_count > 1 ? brick::interfaces::display::RenderMode::direct :
                                         brick::interfaces::display::RenderMode::partial,
    };
}

Status St7701sRgbDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    if (rotation != brick::interfaces::display::Rotation::rotate_0)
    {
        ESP_LOGW(TAG, "ST7701S RGB profile supports only rotation 0");
        return Status::not_supported;
    }
    return Status::ok;
}

Status St7701sRgbDisplay::draw_buffer(brick::interfaces::display::DisplayRect        area,
                                      const brick::interfaces::display::PixelBuffer& buffer)
{
    if (!initialized_ || panel_ == nullptr)
        return Status::not_ready;
    if (area.empty() || area.x < 0 || area.y < 0 || area.x + area.width > config_.width
        || area.y + area.height > config_.height || !buffer.valid()
        || buffer.width != static_cast<std::uint32_t>(area.width)
        || buffer.height != static_cast<std::uint32_t>(area.height) || buffer.format != pixel_format()
        || buffer.stride_bytes != static_cast<std::size_t>(area.width) * 2)
        return Status::invalid_argument;
    return to_status(
        esp_lcd_panel_draw_bitmap(panel_, area.x, area.y, area.x + area.width, area.y + area.height, buffer.data));
}

Status St7701sRgbDisplay::wait_for_transfer_complete(std::uint32_t timeout_ms)
{
    // esp_lcd copies a drawn area into the frame buffer before
    // esp_lcd_panel_draw_bitmap() returns: nothing is left to wait for.
    (void)timeout_ms;
    return Status::ok;
}

Status St7701sRgbDisplay::wait_for_vsync(std::uint32_t timeout_ms)
{
    if (vsync_semaphore_ == nullptr)
        return Status::not_ready;
    // After a late flip the old buffer is sent once more: two VSYNCs.
    const std::uint8_t vsyncs = flip_vsyncs_ > 1 ? flip_vsyncs_ : 1;
    flip_vsyncs_              = 0;
    for (std::uint8_t count = 0; count < vsyncs; ++count)
        if (xSemaphoreTake(vsync_semaphore_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
            return Status::timeout;
    return Status::ok;
}

std::uint8_t St7701sRgbDisplay::frame_buffer_count() const
{
    return config_.frame_buffer_count;
}

Status St7701sRgbDisplay::get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer)
{
    if (!initialized_ || panel_ == nullptr)
        return Status::not_ready;
    if (index >= config_.frame_buffer_count)
        return Status::invalid_argument;

    void* frame_buffers[3] = {};
    if (const esp_err_t err = esp_lcd_rgb_panel_get_frame_buffer(panel_, config_.frame_buffer_count, &frame_buffers[0],
                                                                 &frame_buffers[1], &frame_buffers[2]);
        err != ESP_OK)
        return to_status(err);
    void* frame_buffer = frame_buffers[index];
    if (frame_buffer == nullptr)
        return Status::error;

    buffer = {
        static_cast<std::uint8_t*>(frame_buffer),    config_.width,  config_.height,
        static_cast<std::size_t>(config_.width) * 2, pixel_format(), true,
    };
    return Status::ok;
}

Status St7701sRgbDisplay::present_frame_buffer(std::uint8_t index)
{
    brick::interfaces::display::WritablePixelBuffer buffer;
    if (const Status status = get_frame_buffer(index, buffer); !is_ok(status))
        return status;
    // esp_lcd links the DMA to the new buffer at the end of the buffer it
    // sends. The DMA reads ahead of the panel, so once it has passed that end
    // (in the blanking before the VSYNC) the old buffer is sent for one more
    // frame and is free only at the second VSYNC.
    const std::uint32_t since_vsync =
        static_cast<std::uint32_t>(esp_timer_get_time()) - last_vsync_us_.load(std::memory_order_relaxed);
    flip_vsyncs_ = since_vsync + late_flip_us_ >= frame_period_us_ ? 2 : 1;
    // wait_for_vsync() after the flip has to see a VSYNC that comes after it,
    // not one left from before: until then the old buffer is on the screen.
    (void)xSemaphoreTake(vsync_semaphore_, 0);
    return to_status(esp_lcd_panel_draw_bitmap(panel_, 0, 0, config_.width, config_.height, buffer.data));
}

bool IRAM_ATTR St7701sRgbDisplay::on_vsync_(esp_lcd_panel_handle_t                panel,
                                            const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx)
{
    (void)panel;
    (void)event_data;
    auto*      display                  = static_cast<St7701sRgbDisplay*>(user_ctx);
    BaseType_t high_priority_task_woken = pdFALSE;
    if (display == nullptr)
        return false;
    // esp_timer_get_time() runs from IRAM (CONFIG_ESP_TIMER_IN_IRAM, the default).
    display->last_vsync_us_.store(static_cast<std::uint32_t>(esp_timer_get_time()), std::memory_order_relaxed);
    if (display->vsync_semaphore_ != nullptr)
        xSemaphoreGiveFromISR(display->vsync_semaphore_, &high_priority_task_woken);
    return high_priority_task_woken == pdTRUE;
}

void St7701sRgbDisplay::compute_frame_timing_()
{
    // The DMA reads up to two descriptors (4 KB each, about 8 lines of a
    // 480-pixel RGB565 panel) ahead of the panel.
    constexpr std::uint64_t kReadAheadLines = 8;
    constexpr std::uint64_t kUsPerSecond    = 1000000;
    const std::uint64_t     line_clocks     = static_cast<std::uint64_t>(config_.width) + config_.hsync_pulse_width
                                      + config_.hsync_back_porch + config_.hsync_front_porch;
    const std::uint64_t blank_lines =
        static_cast<std::uint64_t>(config_.vsync_pulse_width) + config_.vsync_back_porch + config_.vsync_front_porch;
    const std::uint64_t frame_lines = config_.height + blank_lines;
    const std::uint64_t pixel_clock = config_.pixel_clock_hz != 0 ? config_.pixel_clock_hz : 1;
    frame_period_us_ = static_cast<std::uint32_t>(line_clocks * frame_lines * kUsPerSecond / pixel_clock);
    // The whole blanking plus the read-ahead covers where the VSYNC event
    // falls in it.
    late_flip_us_ =
        static_cast<std::uint32_t>(line_clocks * (blank_lines + kReadAheadLines) * kUsPerSecond / pixel_clock);
}

Status St7701sRgbDisplay::begin_spi_()
{
    spi_bus_config_t bus = {};
    bus.sclk_io_num      = config_.spi_sclk_gpio;
    bus.mosi_io_num      = config_.spi_mosi_gpio;
    bus.miso_io_num      = config_.spi_miso_gpio;
    bus.quadwp_io_num    = GPIO_NUM_NC;
    bus.quadhd_io_num    = GPIO_NUM_NC;
    bus.max_transfer_sz  = 16;
    auto err             = spi_bus_initialize(config_.spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return to_status(err);
    }
    spi_ready_ = err == ESP_OK;

    spi_device_interface_config_t device = {};
    device.clock_speed_hz                = static_cast<int>(config_.spi_clock_hz);
    device.mode                          = config_.spi_mode;
    device.spics_io_num                  = config_.spi_cs_gpio;
    device.queue_size                    = 1;
    err                                  = spi_bus_add_device(config_.spi_host, &device, &spi_device_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ST7701S SPI device init failed: %s", esp_err_to_name(err));
        return to_status(err);
    }
    return Status::ok;
}

Status St7701sRgbDisplay::write_9bit_(std::uint16_t value)
{
    spi_transaction_ext_t transaction = {};
    transaction.base.flags            = SPI_TRANS_VARIABLE_CMD;
    transaction.command_bits          = 9;
    transaction.base.cmd              = value;
    auto err = spi_device_polling_transmit(spi_device_, reinterpret_cast<spi_transaction_t*>(&transaction));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ST7701S SPI write failed: %s", esp_err_to_name(err));
        return to_status(err);
    }
    return Status::ok;
}

Status St7701sRgbDisplay::send_command_(std::uint8_t command, const std::uint8_t* data, std::size_t length)
{
    if (const Status status = write_9bit_(command); !is_ok(status))
        return status;
    for (std::size_t index = 0; index < length; ++index)
    {
        // Bit 8 set: a parameter byte rather than a command.
        if (const Status status = write_9bit_(static_cast<std::uint16_t>(data[index]) | 0x100); !is_ok(status))
            return status;
    }
    return Status::ok;
}

Status St7701sRgbDisplay::send_init_sequence_()
{
    std::size_t index = 0;
    while (index < config_.init_sequence_size)
    {
        if (config_.init_sequence_size - index < 2)
            return Status::invalid_argument;
        const auto command = config_.init_sequence[index++];
        const auto count   = config_.init_sequence[index++];
        if (count == 0xFF)
        {
            vTaskDelay(pdMS_TO_TICKS(command));
            continue;
        }
        if (config_.init_sequence_size - index < count)
            return Status::invalid_argument;
        if (const Status status = send_command_(command, config_.init_sequence + index, count); !is_ok(status))
        {
            ESP_LOGE(TAG, "ST7701S init command 0x%02X failed", command);
            return status;
        }
        index += count;
        vTaskDelay(1);
    }

    constexpr std::uint8_t bank0[]     = { 0x77, 0x01, 0x00, 0x00, 0x10 };
    constexpr std::uint8_t direction[] = { 0x00 };
    const std::uint8_t     madctl[]    = { static_cast<std::uint8_t>((config_.mirror_x ? 0x04 : 0x00)
                                                              | (config_.mirror_y ? 0x10 : 0x00)) };
    if (const Status status = send_command_(0xFF, bank0, sizeof(bank0)); !is_ok(status))
        return status;
    if (const Status status = send_command_(0xC7, direction, sizeof(direction)); !is_ok(status))
        return status;
    if (const Status status = send_command_(0x36, madctl, sizeof(madctl)); !is_ok(status))
        return status;
    if (const Status status = send_command_(config_.invert_colors ? 0x21 : 0x20, nullptr, 0); !is_ok(status))
        return status;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (const Status status = send_command_(0x11, nullptr, 0); !is_ok(status))  // sleep out
        return status;
    if (const Status status = send_command_(0x29, nullptr, 0); !is_ok(status))  // display on
        return status;
    vTaskDelay(pdMS_TO_TICKS(10));
    return Status::ok;
}

Status St7701sRgbDisplay::begin_rgb_panel_()
{
    constexpr std::uint32_t kMaxFrameBuffers = 3;
    if (config_.frame_buffer_count == 0 || config_.frame_buffer_count > kMaxFrameBuffers)
    {
        ESP_LOGE(TAG, "Unsupported framebuffer count: %u", config_.frame_buffer_count);
        return Status::invalid_argument;
    }
    if (const Status status = create_semaphores_(); !is_ok(status))
        return status;
    if (const Status status = create_rgb_panel_(); !is_ok(status))
        return status;
    reset_panel_();
    esp_err_t err = esp_lcd_panel_reset(panel_);
    if (err == ESP_OK)
        err = esp_lcd_panel_init(panel_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
        return to_status(err);
    }
    return Status::ok;
}

Status St7701sRgbDisplay::create_semaphores_()
{
    vsync_semaphore_ = xSemaphoreCreateBinary();
    if (vsync_semaphore_ == nullptr)
    {
        ESP_LOGE(TAG, "VSYNC semaphore allocation failed");
        return Status::no_memory;
    }
    return Status::ok;
}

Status St7701sRgbDisplay::create_rgb_panel_()
{
    // RGB565 over a 16-bit parallel bus. A bounce buffer of a few lines in
    // internal RAM, when configured, keeps PSRAM reads off the timing path.
    constexpr std::uint32_t    kBusBits        = 16;
    esp_lcd_rgb_panel_config_t panel_config    = {};
    panel_config.clk_src                       = LCD_CLK_SRC_PLL160M;
    panel_config.timings.pclk_hz               = config_.pixel_clock_hz;
    panel_config.timings.h_res                 = config_.width;
    panel_config.timings.v_res                 = config_.height;
    panel_config.timings.hsync_pulse_width     = config_.hsync_pulse_width;
    panel_config.timings.hsync_back_porch      = config_.hsync_back_porch;
    panel_config.timings.hsync_front_porch     = config_.hsync_front_porch;
    panel_config.timings.vsync_pulse_width     = config_.vsync_pulse_width;
    panel_config.timings.vsync_back_porch      = config_.vsync_back_porch;
    panel_config.timings.vsync_front_porch     = config_.vsync_front_porch;
    panel_config.timings.flags.pclk_active_neg = config_.pclk_active_neg;
    panel_config.data_width                    = kBusBits;
    panel_config.bits_per_pixel                = kBusBits;
    panel_config.num_fbs                       = config_.frame_buffer_count;
    panel_config.bounce_buffer_size_px         = static_cast<std::size_t>(config_.width) * config_.bounce_buffer_lines;
    panel_config.flags.fb_in_psram             = 1;
    panel_config.hsync_gpio_num                = config_.hsync_gpio;
    panel_config.vsync_gpio_num                = config_.vsync_gpio;
    panel_config.de_gpio_num                   = config_.de_gpio;
    panel_config.pclk_gpio_num                 = config_.pclk_gpio;
    panel_config.disp_gpio_num                 = GPIO_NUM_NC;
    for (std::size_t index = 0; index < config_.data_gpios.size(); ++index)
        panel_config.data_gpio_nums[index] = config_.data_gpios[index];

    if (const esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &panel_); err != ESP_OK)
    {
        ESP_LOGE(TAG, "RGB panel creation failed: %s", esp_err_to_name(err));
        return to_status(err);
    }
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
    callbacks.on_vsync                            = on_vsync_;
    if (const esp_err_t err = esp_lcd_rgb_panel_register_event_callbacks(panel_, &callbacks, this); err != ESP_OK)
    {
        ESP_LOGE(TAG, "RGB panel callback registration failed: %s", esp_err_to_name(err));
        return to_status(err);
    }
    return Status::ok;
}

void St7701sRgbDisplay::reset_panel_()
{
    constexpr std::uint32_t kResetPulseMs    = 5;
    constexpr std::uint32_t kResetRecoveryMs = 20;
    if (config_.reset_gpio == GPIO_NUM_NC)
        return;
    gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(config_.reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(kResetPulseMs));
    gpio_set_level(config_.reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(kResetRecoveryMs));
}


Status St7701sRgbDisplay::copy_to_frame_buffer(std::uint8_t index, brick::interfaces::display::DisplayRect area,
                                               const brick::interfaces::display::PixelBuffer& pixels)
{
    brick::interfaces::display::WritablePixelBuffer frame;
    if (const Status status = get_frame_buffer(index, frame); !is_ok(status))
        return status;
    return brick::core::display::copy_into_frame(frame, area, pixels);
}

Status St7701sRgbDisplay::copy_between_frame_buffers(std::uint8_t from, std::uint8_t to,
                                                     brick::interfaces::display::DisplayRect area)
{
    brick::interfaces::display::WritablePixelBuffer source;
    brick::interfaces::display::WritablePixelBuffer target;
    if (const Status status = get_frame_buffer(from, source); !is_ok(status))
        return status;
    if (const Status status = get_frame_buffer(to, target); !is_ok(status))
        return status;
    return brick::core::display::copy_frame_area(source, target, area);
}

}  // namespace brick::platform::esp32::s3
