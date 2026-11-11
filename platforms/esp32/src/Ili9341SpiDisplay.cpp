#include "brick/platform/esp32/Ili9341SpiDisplay.h"

#include "brick/core/display/Rgb565Pixels.h"

namespace brick::platform::esp32
{
namespace
{
    /// One ILI9341 command with up to 15 parameter bytes.
    struct PanelCommand
    {
        std::uint8_t command;
        std::uint8_t length;
        std::uint8_t data[15];
    };

    // Sent after the software reset, before MADCTL: display off, 16-bit pixels.
    constexpr PanelCommand kPanelSetup[] = {
        { 0x28, 0, {} },
        { 0x3A, 1, { 0x55 } },
    };

    // Sent after MADCTL: power, VCOM, frame rate, display function and gamma.
    constexpr PanelCommand kPanelTuning[] = {
        { 0xC0, 1, { 0x23 } },
        { 0xC1, 1, { 0x10 } },
        { 0xC5, 2, { 0x3E, 0x28 } },
        { 0xC7, 1, { 0x86 } },
        { 0xB1, 2, { 0x00, 0x18 } },
        { 0xB6, 3, { 0x08, 0x82, 0x27 } },
        { 0xE0, 15, { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 } },
        { 0xE1, 15, { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F } },
    };

    constexpr std::uint8_t kSoftwareReset = 0x01;
    constexpr std::uint8_t kMemoryAccess  = 0x36;
    constexpr std::uint8_t kSleepOut      = 0x11;
    constexpr std::uint8_t kDisplayOn     = 0x29;
    constexpr std::uint8_t kColumnAddress = 0x2A;
    constexpr std::uint8_t kRowAddress    = 0x2B;
    constexpr std::uint8_t kMemoryWrite   = 0x2C;

    // Longest wait for one queued SPI transfer (rule H1: no unbounded waits).
    constexpr TickType_t kTransferTimeoutTicks = pdMS_TO_TICKS(1000);
}  // namespace

Ili9341SpiDisplay::Ili9341SpiDisplay(Ili9341SpiDisplayConfig config) : config_(config)
{
}

Ili9341SpiDisplay::~Ili9341SpiDisplay()
{
    if (spi_device_ != nullptr)
    {
        // The device cannot be removed with transfers still queued.
        (void)drain_();
        spi_bus_remove_device(spi_device_);
    }
    if (spi_ready_)
        spi_bus_free(config_.spi_host);
}

Status Ili9341SpiDisplay::begin()
{
    if (started_)
        return Status::ok;
    if (config_.sclk_gpio == GPIO_NUM_NC || config_.mosi_gpio == GPIO_NUM_NC || config_.cs_gpio == GPIO_NUM_NC
        || config_.dc_gpio == GPIO_NUM_NC)
        return Status::invalid_argument;
    gpio_set_direction(config_.dc_gpio, GPIO_MODE_OUTPUT);
    if (config_.backlight_gpio != GPIO_NUM_NC)
    {
        gpio_set_direction(config_.backlight_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(config_.backlight_gpio, config_.backlight_active_high ? 1 : 0);
    }
    if (const Status status = begin_spi_(); !is_ok(status))
        return status;
    if (const Status status = initialize_panel_(); !is_ok(status))
        return status;
    started_ = true;
    ESP_LOGI(TAG, "ILI9341 SPI display ready %ux%u", config_.width, config_.height);
    return Status::ok;
}

brick::interfaces::display::DisplaySize Ili9341SpiDisplay::size() const
{
    return { config_.width, config_.height };
}

brick::interfaces::display::PixelFormat Ili9341SpiDisplay::pixel_format() const
{
    return brick::interfaces::display::PixelFormat::rgb565;
}

brick::interfaces::display::DisplayCapabilities Ili9341SpiDisplay::capabilities() const
{
    return {
        brick::interfaces::display::DisplayPanelType::spi,
        { config_.width, config_.height },
        pixel_format(),
        static_cast<std::size_t>(config_.width) * config_.height
            * brick::interfaces::display::pixel_format_bytes(pixel_format()),
        4,
        kPixelTransferBytes,
        true,
        false,
        false,
        true,
        false,
        1,
        brick::interfaces::display::RenderMode::partial,
    };
}

Status Ili9341SpiDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    if (rotation != brick::interfaces::display::Rotation::rotate_0)
    {
        ESP_LOGW(TAG, "ILI9341 smoke profile supports only rotation 0");
        return Status::not_supported;
    }
    if (const Status status = drain_(); !is_ok(status))
        return status;
    return send_command_(kMemoryAccess, &config_.madctl, 1);
}

Status Ili9341SpiDisplay::draw_buffer(brick::interfaces::display::DisplayRect        area,
                                      const brick::interfaces::display::PixelBuffer& buffer)
{
    constexpr std::size_t bytes_per_pixel = 2;
    if (!started_)
        return Status::not_ready;
    if (area.empty() || area.x < 0 || area.y < 0 || area.x + area.width > config_.width
        || area.y + area.height > config_.height || !buffer.valid()
        || buffer.width != static_cast<std::uint32_t>(area.width)
        || buffer.height != static_cast<std::uint32_t>(area.height) || buffer.format != pixel_format()
        || buffer.stride_bytes < static_cast<std::size_t>(area.width) * bytes_per_pixel)
        return Status::invalid_argument;

    // The previous area's last transfers end before the window changes.
    if (const Status status = drain_(); !is_ok(status))
        return status;
    if (const Status status =
            set_address_window_(static_cast<std::uint16_t>(area.x), static_cast<std::uint16_t>(area.y),
                                static_cast<std::uint16_t>(area.width), static_cast<std::uint16_t>(area.height));
        !is_ok(status))
        return status;
    const Status status = send_pixels_(buffer, static_cast<std::size_t>(area.width) * bytes_per_pixel);
    if (!is_ok(status))
        (void)drain_();
    return status;
}

Status Ili9341SpiDisplay::fill_rect(brick::interfaces::display::DisplayRect area, std::uint16_t color)
{
    if (!started_)
        return Status::not_ready;
    if (!inside_(area))
        return Status::invalid_argument;
    if (const Status status = drain_(); !is_ok(status))
        return status;
    if (const Status status =
            set_address_window_(static_cast<std::uint16_t>(area.x), static_cast<std::uint16_t>(area.y),
                                static_cast<std::uint16_t>(area.width), static_cast<std::uint16_t>(area.height));
        !is_ok(status))
        return status;

    // One buffer with the color in the panel's byte order is sent as often
    // as needed; concurrent transfers may read the same buffer.
    const std::size_t total_bytes = static_cast<std::size_t>(area.width) * static_cast<std::size_t>(area.height) * 2U;
    const std::size_t chunk_bytes = std::min(total_bytes, kPixelTransferBytes);
    auto&             pixels      = pixel_tx_buffers_[0];
    brick::core::display::fill_rgb565(reinterpret_cast<std::uint16_t*>(pixels.data()), chunk_bytes / 2U,
                                      static_cast<std::uint16_t>((color >> 8) | (color << 8)));
    const Status status = send_repeated_(pixels.data(), chunk_bytes, total_bytes);
    if (!is_ok(status))
        (void)drain_();
    return status;
}

bool Ili9341SpiDisplay::inside_(brick::interfaces::display::DisplayRect area) const
{
    return !area.empty() && area.x >= 0 && area.y >= 0 && area.x + area.width <= config_.width
           && area.y + area.height <= config_.height;
}

Status Ili9341SpiDisplay::begin_spi_()
{
    spi_bus_config_t bus = {};
    bus.sclk_io_num      = config_.sclk_gpio;
    bus.mosi_io_num      = config_.mosi_gpio;
    bus.miso_io_num      = config_.miso_gpio;
    bus.quadwp_io_num    = GPIO_NUM_NC;
    bus.quadhd_io_num    = GPIO_NUM_NC;
    bus.max_transfer_sz  = static_cast<int>(kPixelTransferBytes);
    // ESP_ERR_INVALID_STATE: the bus is already initialized by another device.
    const esp_err_t err = spi_bus_initialize(config_.spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return to_status(err);
    spi_ready_                           = err == ESP_OK;
    spi_device_interface_config_t device = {};
    device.clock_speed_hz                = static_cast<int>(config_.spi_clock_hz);
    device.mode                          = config_.spi_mode;
    device.spics_io_num                  = config_.cs_gpio;
    device.queue_size                    = 2;
    return to_status(spi_bus_add_device(config_.spi_host, &device, &spi_device_));
}

Status Ili9341SpiDisplay::transmit_(bool data, const std::uint8_t* bytes, std::size_t length)
{
    gpio_set_level(config_.dc_gpio, data ? 1 : 0);
    spi_transaction_t transaction = {};
    transaction.length            = length * 8;
    transaction.tx_buffer         = bytes;
    return to_status(spi_device_polling_transmit(spi_device_, &transaction));
}

Status Ili9341SpiDisplay::send_command_(std::uint8_t command, const std::uint8_t* args, std::size_t length)
{
    if (const Status status = transmit_(false, &command, 1); !is_ok(status))
        return status;
    return length == 0 ? Status::ok : transmit_(true, args, length);
}

Status Ili9341SpiDisplay::send_pixels_(const brick::interfaces::display::PixelBuffer& buffer, std::size_t row_bytes)
{
    gpio_set_level(config_.dc_gpio, 1);
    // Rows are packed into full transfer buffers, also when the caller's rows
    // have stride padding, so a narrow area still moves in 4 KB transfers.
    std::uint32_t row        = 0;
    std::size_t   row_offset = 0;
    while (row < buffer.height)
    {
        if (const Status status = reserve_transaction_(); !is_ok(status))
            return status;
        auto&       pixels = pixel_tx_buffers_[next_transaction_];
        std::size_t filled = 0;
        while (filled < kPixelTransferBytes && row < buffer.height)
        {
            const std::size_t chunk = std::min(kPixelTransferBytes - filled, row_bytes - row_offset);
            // RGB565 is little-endian in memory; ILI9341 takes every pixel MSB first.
            brick::core::display::copy_rgb565_swapped(
                pixels.data() + filled, buffer.data + static_cast<std::size_t>(row) * buffer.stride_bytes + row_offset,
                chunk);
            filled += chunk;
            row_offset += chunk;
            if (row_offset == row_bytes)
            {
                ++row;
                row_offset = 0;
            }
        }
        if (const Status status = queue_transaction_(pixels.data(), filled); !is_ok(status))
            return status;
    }
    return Status::ok;
}

Status Ili9341SpiDisplay::send_repeated_(const std::uint8_t* data, std::size_t chunk_bytes, std::size_t total_bytes)
{
    gpio_set_level(config_.dc_gpio, 1);
    while (total_bytes != 0)
    {
        if (const Status status = reserve_transaction_(); !is_ok(status))
            return status;
        const std::size_t length = std::min(total_bytes, chunk_bytes);
        if (const Status status = queue_transaction_(data, length); !is_ok(status))
            return status;
        total_bytes -= length;
    }
    return Status::ok;
}

Status Ili9341SpiDisplay::reserve_transaction_()
{
    // Every descriptor is in flight: the oldest one, next_transaction_, has
    // to finish before it and its buffer are used again.
    if (queued_ < kPixelBufferCount)
        return Status::ok;
    return collect_transaction_();
}

Status Ili9341SpiDisplay::queue_transaction_(const std::uint8_t* data, std::size_t length)
{
    spi_transaction_t& transaction = pixel_transactions_[next_transaction_];
    transaction                    = {};
    transaction.length             = length * 8;  // in bits
    transaction.tx_buffer          = data;
    if (const esp_err_t err = spi_device_queue_trans(spi_device_, &transaction, kTransferTimeoutTicks); err != ESP_OK)
        return to_status(err);
    ++queued_;
    next_transaction_ = (next_transaction_ + 1) % kPixelBufferCount;
    return Status::ok;
}

Status Ili9341SpiDisplay::collect_transaction_()
{
    spi_transaction_t* completed = nullptr;
    const esp_err_t    err       = spi_device_get_trans_result(spi_device_, &completed, kTransferTimeoutTicks);
    if (err == ESP_OK)
        --queued_;
    return to_status(err);
}

Status Ili9341SpiDisplay::drain_()
{
    while (queued_ != 0)
    {
        if (const Status status = collect_transaction_(); !is_ok(status))
        {
            // A transfer that did not finish within the timeout is not
            // waited for again; the first failure is reported.
            queued_ = 0;
            return status;
        }
    }
    return Status::ok;
}

Status Ili9341SpiDisplay::set_address_window_(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                                              std::uint16_t height)
{
    const std::uint16_t x2       = x + width - 1;
    const std::uint16_t y2       = y + height - 1;
    const std::uint8_t  column[] = { static_cast<std::uint8_t>(x >> 8), static_cast<std::uint8_t>(x),
                                     static_cast<std::uint8_t>(x2 >> 8), static_cast<std::uint8_t>(x2) };
    const std::uint8_t  row[]    = { static_cast<std::uint8_t>(y >> 8), static_cast<std::uint8_t>(y),
                                     static_cast<std::uint8_t>(y2 >> 8), static_cast<std::uint8_t>(y2) };
    if (const Status status = send_command_(kColumnAddress, column, sizeof(column)); !is_ok(status))
        return status;
    if (const Status status = send_command_(kRowAddress, row, sizeof(row)); !is_ok(status))
        return status;
    return send_command_(kMemoryWrite);
}

Status Ili9341SpiDisplay::initialize_panel_()
{
    if (const Status status = send_command_(kSoftwareReset); !is_ok(status))
        return status;
    vTaskDelay(pdMS_TO_TICKS(120));
    for (const PanelCommand& entry : kPanelSetup)
        if (const Status status = send_command_(entry.command, entry.data, entry.length); !is_ok(status))
            return status;
    if (const Status status = send_command_(kMemoryAccess, &config_.madctl, 1); !is_ok(status))
        return status;
    for (const PanelCommand& entry : kPanelTuning)
        if (const Status status = send_command_(entry.command, entry.data, entry.length); !is_ok(status))
            return status;
    if (const Status status = send_command_(kSleepOut); !is_ok(status))
        return status;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (const Status status = send_command_(kDisplayOn); !is_ok(status))
        return status;
    vTaskDelay(pdMS_TO_TICKS(20));
    return Status::ok;
}

}  // namespace brick::platform::esp32
