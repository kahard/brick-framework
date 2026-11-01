#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/communication/ISerialPort.h"
#include "brick/platform/stm32/f1/Gpio.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct Stm32UartConfig
{
    USART_TypeDef* instance = nullptr;
    GpioPin        tx;
    GpioPin        rx;  ///< optional: not connected for a transmit-only port
    std::uint32_t  baud_rate        = 115200;
    std::uint32_t  write_timeout_ms = 100;
};

/// 8N1 UART without interrupts: write() blocks until the bytes are sent,
/// read() returns the byte waiting in the receiver, if any (poll it often).
class Stm32Uart final : public brick::interfaces::communication::ISerialPort
{
public:
    explicit Stm32Uart(Stm32UartConfig config) : config_(config) {}

    Status      begin() override;
    std::size_t read(std::uint8_t* buffer, std::size_t capacity) override;
    std::size_t write(const std::uint8_t* data, std::size_t length) override;

private:
    Stm32UartConfig    config_;
    UART_HandleTypeDef handle_{};
    bool               started_ = false;
};

}  // namespace brick::platform::stm32::f1
