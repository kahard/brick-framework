#include "brick/platform/esp32/s3/St7701sRgbDisplay.h"

#include "esp_log.h"
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
    if (transfer_semaphore_ != nullptr)
        vSemaphoreDelete(transfer_semaphore_);
    if (panel_ != nullptr)
        esp_lcd_panel_del(panel_);
    if (spi_device_ != nullptr)
        spi_bus_remove_device(spi_device_);
    if (spi_ready_)
        spi_bus_free(config_.spi_host);
}

bool St7701sRgbDisplay::begin()
{
    if (initialized_)
        return true;
    if (config_.width == 0 || config_.height == 0)
        return false;

    ESP_LOGI(TAG, "begin ST7701S RGB %ux%u pclk=%luHz", config_.width, config_.height, static_cast<unsigned long>(config_.pixel_clock_hz));
    if (!begin_spi_() || !send_init_sequence_())
        return false;
    if (!begin_rgb_panel_())
        return false;
    if (config_.backlight_gpio != GPIO_NUM_NC)
    {
        gpio_set_direction(config_.backlight_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config_.backlight_gpio, config_.backlight_active_high ? 1 : 0);
    }
    initialized_ = true;
    return true;
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
        static_cast<std::size_t>(config_.width) * config_.height * brick::interfaces::display::pixel_format_bytes(pixel_format()),
        4,
        0,
        true,
        true,
        config_.frame_buffer_count > 1,
        true,
        false,
        config_.frame_buffer_count,
        config_.frame_buffer_count > 1 ? brick::interfaces::display::RenderMode::direct : brick::interfaces::display::RenderMode::partial,
    };
}

bool St7701sRgbDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    if (rotation != brick::interfaces::display::Rotation::rotate_0)
    {
        ESP_LOGW(TAG, "ST7701S RGB profile supports only rotation 0");
        return false;
    }
    return true;
}

bool St7701sRgbDisplay::draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer)
{
    if (!initialized_ || panel_ == nullptr || area.empty() || area.x < 0 || area.y < 0 || area.x + area.width > config_.width || area.y + area.height > config_.height ||
        !buffer.valid() || buffer.width != static_cast<std::uint32_t>(area.width) || buffer.height != static_cast<std::uint32_t>(area.height) ||
        buffer.format != pixel_format() || buffer.stride_bytes != static_cast<std::size_t>(area.width) * 2)
        return false;
    return esp_lcd_panel_draw_bitmap(panel_, area.x, area.y, area.x + area.width, area.y + area.height, buffer.data) == ESP_OK;
}

bool St7701sRgbDisplay::wait_for_transfer_complete(std::uint32_t timeout_ms)
{
    if (transfer_semaphore_ == nullptr)
        return true;
    return xSemaphoreTake(transfer_semaphore_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool St7701sRgbDisplay::wait_for_vsync(std::uint32_t timeout_ms)
{
    if (vsync_semaphore_ == nullptr)
        return false;
    return xSemaphoreTake(vsync_semaphore_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

std::uint8_t St7701sRgbDisplay::frame_buffer_count() const
{
    return config_.frame_buffer_count;
}

bool St7701sRgbDisplay::get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer)
{
    if (!initialized_ || panel_ == nullptr || index >= config_.frame_buffer_count)
        return false;

    void* frame_buffers[3] = {};
    if (esp_lcd_rgb_panel_get_frame_buffer(panel_, config_.frame_buffer_count,
                                           &frame_buffers[0], &frame_buffers[1], &frame_buffers[2]) != ESP_OK)
        return false;
    void* frame_buffer = frame_buffers[index];
    if (frame_buffer == nullptr)
        return false;

    buffer = {
        static_cast<std::uint8_t*>(frame_buffer),
        config_.width,
        config_.height,
        static_cast<std::size_t>(config_.width) * 2,
        pixel_format(),
        true,
    };
    return true;
}

bool St7701sRgbDisplay::present_frame_buffer(std::uint8_t index)
{
    if (!initialized_ || panel_ == nullptr || index >= config_.frame_buffer_count)
        return false;

    brick::interfaces::display::WritablePixelBuffer buffer;
    if (!get_frame_buffer(index, buffer))
        return false;
    return esp_lcd_panel_draw_bitmap(panel_, 0, 0, config_.width, config_.height, buffer.data) == ESP_OK;
}

bool IRAM_ATTR St7701sRgbDisplay::on_vsync_(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx)
{
    (void)panel;
    (void)event_data;
    auto* display = static_cast<St7701sRgbDisplay*>(user_ctx);
    BaseType_t high_priority_task_woken = pdFALSE;
    if (display != nullptr && display->vsync_semaphore_ != nullptr)
        xSemaphoreGiveFromISR(display->vsync_semaphore_, &high_priority_task_woken);
    return high_priority_task_woken == pdTRUE;
}

bool IRAM_ATTR St7701sRgbDisplay::on_color_trans_done_(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx)
{
    (void)panel;
    (void)event_data;
    auto* display = static_cast<St7701sRgbDisplay*>(user_ctx);
    BaseType_t high_priority_task_woken = pdFALSE;
    if (display != nullptr && display->transfer_semaphore_ != nullptr)
        xSemaphoreGiveFromISR(display->transfer_semaphore_, &high_priority_task_woken);
    return high_priority_task_woken == pdTRUE;
}

bool St7701sRgbDisplay::begin_spi_()
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
        return false;
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
        return false;
    }
    return true;
}

bool St7701sRgbDisplay::write_9bit_(std::uint16_t value)
{
    spi_transaction_ext_t transaction = {};
    transaction.base.flags            = SPI_TRANS_VARIABLE_CMD;
    transaction.command_bits          = 9;
    transaction.base.cmd              = value;
    auto err                          = spi_device_polling_transmit(spi_device_, reinterpret_cast<spi_transaction_t*>(&transaction));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ST7701S SPI write failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool St7701sRgbDisplay::send_command_(std::uint8_t command, const std::uint8_t* data, std::size_t length)
{
    if (!write_9bit_(command))
        return false;
    for (std::size_t index = 0; index < length; ++index)
    {
        if (!write_9bit_(static_cast<std::uint16_t>(data[index]) | 0x100))
            return false;
    }
    return true;
}

bool St7701sRgbDisplay::send_init_sequence_()
{
    std::size_t index = 0;
    while (index < config_.init_sequence_size)
    {
        if (config_.init_sequence_size - index < 2)
            return false;
        const auto command = config_.init_sequence[index++];
        const auto count   = config_.init_sequence[index++];
        if (count == 0xFF)
        {
            vTaskDelay(pdMS_TO_TICKS(command));
            continue;
        }
        if (config_.init_sequence_size - index < count || !send_command_(command, config_.init_sequence + index, count))
        {
            ESP_LOGE(TAG, "ST7701S init command 0x%02X failed", command);
            return false;
        }
        index += count;
        vTaskDelay(1);
    }

    constexpr std::uint8_t bank0[]     = { 0x77, 0x01, 0x00, 0x00, 0x10 };
    constexpr std::uint8_t direction[] = { 0x00 };
    const std::uint8_t     madctl[]    = { static_cast<std::uint8_t>((config_.mirror_x ? 0x04 : 0x00) | (config_.mirror_y ? 0x10 : 0x00)) };
    if (!send_command_(0xFF, bank0, sizeof(bank0)) || !send_command_(0xC7, direction, sizeof(direction)) || !send_command_(0x36, madctl, sizeof(madctl)) || !send_command_(config_.invert_colors ? 0x21 : 0x20, nullptr, 0))
        return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!send_command_(0x11, nullptr, 0) || !send_command_(0x29, nullptr, 0))
        return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

bool St7701sRgbDisplay::begin_rgb_panel_()
{
    if (config_.frame_buffer_count == 0 || config_.frame_buffer_count > 3)
    {
        ESP_LOGE(TAG, "Unsupported framebuffer count: %u", config_.frame_buffer_count);
        return false;
    }

    vsync_semaphore_ = xSemaphoreCreateBinary();
    if (vsync_semaphore_ == nullptr)
    {
        ESP_LOGE(TAG, "VSYNC semaphore allocation failed");
        return false;
    }
    transfer_semaphore_ = xSemaphoreCreateBinary();
    if (transfer_semaphore_ == nullptr)
    {
        ESP_LOGE(TAG, "Transfer semaphore allocation failed");
        return false;
    }

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
    panel_config.data_width                    = 16;
    panel_config.bits_per_pixel                = 16;
    panel_config.num_fbs                       = config_.frame_buffer_count;
    panel_config.bounce_buffer_size_px         = config_.frame_buffer_count > 1 ? 0 : config_.width * 10;
    panel_config.flags.fb_in_psram             = 1;
    panel_config.hsync_gpio_num                = config_.hsync_gpio;
    panel_config.vsync_gpio_num                = config_.vsync_gpio;
    panel_config.de_gpio_num                   = config_.de_gpio;
    panel_config.pclk_gpio_num                 = config_.pclk_gpio;
    panel_config.disp_gpio_num                 = GPIO_NUM_NC;
    for (std::size_t index = 0; index < config_.data_gpios.size(); ++index)
        panel_config.data_gpio_nums[index] = config_.data_gpios[index];

    auto err = esp_lcd_new_rgb_panel(&panel_config, &panel_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "RGB panel creation failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
    callbacks.on_color_trans_done = on_color_trans_done_;
    callbacks.on_vsync = on_vsync_;
    err = esp_lcd_rgb_panel_register_event_callbacks(panel_, &callbacks, this);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "RGB panel callback registration failed: %s", esp_err_to_name(err));
        return false;
    }
    if (config_.reset_gpio != GPIO_NUM_NC)
    {
        gpio_set_direction(config_.reset_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config_.reset_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(config_.reset_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    err = esp_lcd_panel_reset(panel_);
    if (err == ESP_OK)
        err = esp_lcd_panel_init(panel_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

}  // namespace brick::platform::esp32::s3
