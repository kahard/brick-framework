#include "brick/platform/esp32/Ili9341SpiDisplay.h"

namespace brick::platform::esp32
{

Ili9341SpiDisplay::Ili9341SpiDisplay(Ili9341SpiDisplayConfig config) : config_(config)
{
}

Ili9341SpiDisplay::~Ili9341SpiDisplay()
{
    if (spi_device_ != nullptr)
        spi_bus_remove_device(spi_device_);
    if (spi_ready_)
        spi_bus_free(config_.spi_host);
}

bool Ili9341SpiDisplay::begin()
{
    if (started_)
        return true;
    if (config_.sclk_gpio == GPIO_NUM_NC || config_.mosi_gpio == GPIO_NUM_NC || config_.cs_gpio == GPIO_NUM_NC || config_.dc_gpio == GPIO_NUM_NC)
        return false;
    gpio_set_direction(config_.dc_gpio, GPIO_MODE_OUTPUT);
    if (config_.backlight_gpio != GPIO_NUM_NC)
    {
        gpio_set_direction(config_.backlight_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config_.backlight_gpio, config_.backlight_active_high ? 1 : 0);
    }
    if (!begin_spi_() || !initialize_panel_())
        return false;
    started_ = true;
    ESP_LOGI(TAG, "ILI9341 SPI display ready %ux%u", config_.width, config_.height);
    return true;
}

brick::interfaces::display::DisplaySize Ili9341SpiDisplay::size() const
{
    return { config_.width, config_.height };
}

brick::interfaces::display::PixelFormat Ili9341SpiDisplay::pixel_format() const
{
    return brick::interfaces::display::PixelFormat::rgb565;
}

bool Ili9341SpiDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    if (rotation != brick::interfaces::display::Rotation::rotate_0)
    {
        ESP_LOGW(TAG, "ILI9341 smoke profile supports only rotation 0");
        return false;
    }
    return send_command_(0x36, { config_.madctl });
}

bool Ili9341SpiDisplay::draw_pixels(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height, const std::uint8_t* pixels, std::size_t byte_count)
{
    if (!started_ || pixels == nullptr || width == 0 || height == 0 || x + width > config_.width || y + height > config_.height || byte_count < static_cast<std::size_t>(width) * height * 2)
        return false;
    const std::uint16_t x2 = x + width - 1, y2 = y + height - 1;
    const std::uint8_t  column[] = { static_cast<std::uint8_t>(x >> 8), static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(x2 >> 8), static_cast<std::uint8_t>(x2) };
    const std::uint8_t  row[]    = { static_cast<std::uint8_t>(y >> 8), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(y2 >> 8), static_cast<std::uint8_t>(y2) };
    return send_command_(0x2A, column, sizeof(column)) && send_command_(0x2B, row, sizeof(row)) && send_command_(0x2C) && send_data_(pixels, static_cast<std::size_t>(width) * height * 2);
}

bool Ili9341SpiDisplay::begin_spi_()
{
    spi_bus_config_t bus = {};
    bus.sclk_io_num      = config_.sclk_gpio;
    bus.mosi_io_num      = config_.mosi_gpio;
    bus.miso_io_num      = config_.miso_gpio;
    bus.quadwp_io_num    = GPIO_NUM_NC;
    bus.quadhd_io_num    = GPIO_NUM_NC;
    bus.max_transfer_sz  = 4096;
    auto err             = spi_bus_initialize(config_.spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return false;
    spi_ready_                           = err == ESP_OK;
    spi_device_interface_config_t device = {};
    device.clock_speed_hz                = static_cast<int>(config_.spi_clock_hz);
    device.mode                          = config_.spi_mode;
    device.spics_io_num                  = config_.cs_gpio;
    device.queue_size                    = 1;
    return spi_bus_add_device(config_.spi_host, &device, &spi_device_) == ESP_OK;
}

bool Ili9341SpiDisplay::transmit_(bool data, const std::uint8_t* bytes, std::size_t length)
{
    gpio_set_level(config_.dc_gpio, data ? 1 : 0);
    spi_transaction_t transaction = {};
    transaction.length            = length * 8;
    transaction.tx_buffer         = bytes;
    return spi_device_polling_transmit(spi_device_, &transaction) == ESP_OK;
}

bool Ili9341SpiDisplay::send_command_(std::uint8_t command)
{
    return transmit_(false, &command, 1);
}

bool Ili9341SpiDisplay::send_command_(std::uint8_t command, std::initializer_list<std::uint8_t> args)
{
    return send_command_(command) && (args.size() == 0 || transmit_(true, args.begin(), args.size()));
}

bool Ili9341SpiDisplay::send_command_(std::uint8_t command, const std::uint8_t* args, std::size_t length)
{
    return send_command_(command) && (length == 0 || transmit_(true, args, length));
}

bool Ili9341SpiDisplay::send_data_(const std::uint8_t* data, std::size_t length)
{
    constexpr std::size_t kChunk = 4096;
    while (length != 0)
    {
        const auto chunk = std::min(length, kChunk);
        if (!transmit_(true, data, chunk))
            return false;
        data += chunk;
        length -= chunk;
    }
    return true;
}

bool Ili9341SpiDisplay::initialize_panel_()
{
    if (!send_command_(0x01))
        return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!send_command_(0x28) || !send_command_(0x3A, { 0x55 }) || !send_command_(0x36, { config_.madctl }) || !send_command_(0xC0, { 0x23 }) || !send_command_(0xC1, { 0x10 }) || !send_command_(0xC5, { 0x3E, 0x28 }) || !send_command_(0xC7, { 0x86 })
        || !send_command_(0xB1, { 0x00, 0x18 }) || !send_command_(0xB6, { 0x08, 0x82, 0x27 }) || !send_command_(0xE0, { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 })
        || !send_command_(0xE1, { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F }) || !send_command_(0x11))
        return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!send_command_(0x29))
        return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

}  // namespace brick::platform::esp32
