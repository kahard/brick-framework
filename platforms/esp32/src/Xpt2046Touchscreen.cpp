#include "brick/platform/esp32/touch/Xpt2046Touchscreen.h"

namespace brick::platform::esp32::touch
{

Xpt2046Touchscreen::Xpt2046Touchscreen(Xpt2046Config config) : config_(config)
{
}

Xpt2046Touchscreen::~Xpt2046Touchscreen()
{
    if (spi_device_ != nullptr)
        spi_bus_remove_device(spi_device_);
    if (spi_ready_)
        spi_bus_free(config_.spi_host);
}

bool Xpt2046Touchscreen::begin()
{
    if (started_)
        return true;
    if (config_.sclk_gpio == GPIO_NUM_NC || config_.mosi_gpio == GPIO_NUM_NC || config_.miso_gpio == GPIO_NUM_NC || config_.cs_gpio == GPIO_NUM_NC || config_.display_size.width == 0 || config_.display_size.height == 0)
        return false;
    if (config_.interrupt_gpio != GPIO_NUM_NC)
    {
        gpio_config_t input = {};
        input.pin_bit_mask  = 1ULL << config_.interrupt_gpio;
        input.mode          = GPIO_MODE_INPUT;
        const bool input_only_pad = config_.interrupt_gpio >= GPIO_NUM_34 &&
                                    config_.interrupt_gpio <= GPIO_NUM_39;
        input.pull_up_en    = input_only_pad ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE;
        if (gpio_config(&input) != ESP_OK)
            return false;
    }
    spi_bus_config_t bus = {};
    bus.sclk_io_num      = config_.sclk_gpio;
    bus.mosi_io_num      = config_.mosi_gpio;
    bus.miso_io_num      = config_.miso_gpio;
    bus.quadwp_io_num    = GPIO_NUM_NC;
    bus.quadhd_io_num    = GPIO_NUM_NC;
    bus.max_transfer_sz  = 8;
    auto err             = spi_bus_initialize(config_.spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return false;
    spi_ready_                           = err == ESP_OK;
    spi_device_interface_config_t device = {};
    device.clock_speed_hz                = static_cast<int>(config_.spi_clock_hz);
    device.mode                          = 0;
    device.spics_io_num                  = config_.cs_gpio;
    device.queue_size                    = 1;
    if (spi_bus_add_device(config_.spi_host, &device, &spi_device_) != ESP_OK)
        return false;
    mapper_  = brick::core::input::TouchMapper(config_.display_size, config_.calibration);
    started_ = true;
    return true;
}

bool Xpt2046Touchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (!started_ || points == nullptr || capacity == 0)
        return false;
    const bool touched = config_.interrupt_gpio == GPIO_NUM_NC || gpio_get_level(config_.interrupt_gpio) == 0;
    if (!touched)
    {
        if (active_)
        {
            points[0]       = last_point_;
            points[0].state = brick::interfaces::display::TouchState::released;
            count           = 1;
            active_         = false;
        }
        return true;
    }
    std::uint16_t raw_x = 0, raw_y = 0, pressure = 0;
    if (!read_sample_(0xD0, raw_x) || !read_sample_(0x90, raw_y) || !read_sample_(0xB0, pressure))
        return false;
    if (pressure < config_.threshold)
        return true;
    auto point  = mapper_.map(0, static_cast<std::int16_t>(raw_x), static_cast<std::int16_t>(raw_y), static_cast<std::int16_t>(pressure));
    point.state = active_ ? brick::interfaces::display::TouchState::moved : brick::interfaces::display::TouchState::pressed;
    points[0]   = point;
    last_point_ = point;
    active_     = true;
    count       = 1;
    return true;
}

bool Xpt2046Touchscreen::read_sample_(std::uint8_t command, std::uint16_t& value)
{
    spi_transaction_t transaction = {};
    transaction.flags             = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    transaction.length            = 24;
    transaction.tx_data[0]        = command;
    if (spi_device_polling_transmit(spi_device_, &transaction) != ESP_OK)
        return false;
    value = static_cast<std::uint16_t>((transaction.rx_data[1] << 8) | transaction.rx_data[2]);
    value = static_cast<std::uint16_t>((value >> 3) & 0x0FFF);
    return true;
}

}  // namespace brick::platform::esp32::touch
