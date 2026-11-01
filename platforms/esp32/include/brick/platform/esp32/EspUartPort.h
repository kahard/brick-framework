#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/communication/ISerialPort.h"
#include "brick/platform/esp32/EspStatus.h"
#include "driver/uart.h"
#include "esp_err.h"

namespace brick::platform::esp32
{

/// ISerialPort on an ESP-IDF UART. UART0 is usually already owned by the
/// console; the existing driver is then reused for reading and writing.
class EspUartPort final : public brick::interfaces::communication::ISerialPort
{
public:
    explicit EspUartPort(uart_port_t port = UART_NUM_0, std::size_t receive_buffer_bytes = 2048U)
        : port_(port), receive_buffer_bytes_(receive_buffer_bytes)
    {
    }

    Status begin() override
    {
        // ESP_ERR_INVALID_STATE: the console already installed the driver; reuse it.
        const esp_err_t result = uart_driver_install(port_, static_cast<int>(receive_buffer_bytes_), 0, 0, nullptr, 0);
        started_               = result == ESP_OK || result == ESP_ERR_INVALID_STATE;
        return started_ ? Status::ok : to_status(result);
    }

    std::size_t read(std::uint8_t* buffer, std::size_t capacity) override
    {
        if (!started_ || buffer == nullptr || capacity == 0)
            return 0;
        const int count = uart_read_bytes(port_, buffer, static_cast<std::uint32_t>(capacity), 0);
        return count > 0 ? static_cast<std::size_t>(count) : 0U;
    }

    std::size_t write(const std::uint8_t* data, std::size_t length) override
    {
        if (!started_ || data == nullptr || length == 0)
            return 0;
        const int count = uart_write_bytes(port_, data, length);
        return count > 0 ? static_cast<std::size_t>(count) : 0U;
    }

private:
    uart_port_t port_;
    std::size_t receive_buffer_bytes_;
    bool        started_ = false;
};

}  // namespace brick::platform::esp32
