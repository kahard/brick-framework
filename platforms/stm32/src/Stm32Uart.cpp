#include "brick/platform/stm32/f1/Stm32Uart.h"

#include "brick/platform/stm32/f1/HalStatus.h"

namespace brick::platform::stm32::f1
{

Status Stm32Uart::begin()
{
    if (started_)
        return Status::ok;
    if (config_.instance == nullptr || !config_.tx.connected() || config_.baud_rate == 0)
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.instance); !is_ok(status))
        return status;
    configure(config_.tx, GPIO_MODE_AF_PP);
    configure(config_.rx, GPIO_MODE_INPUT);
    handle_.Instance          = config_.instance;
    handle_.Init.BaudRate     = config_.baud_rate;
    handle_.Init.WordLength   = UART_WORDLENGTH_8B;
    handle_.Init.StopBits     = UART_STOPBITS_1;
    handle_.Init.Parity       = UART_PARITY_NONE;
    handle_.Init.Mode         = config_.rx.connected() ? UART_MODE_TX_RX : UART_MODE_TX;
    handle_.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    handle_.Init.OverSampling = UART_OVERSAMPLING_16;
    const Status status       = to_status(HAL_UART_Init(&handle_));
    started_                  = is_ok(status);
    return status;
}

std::size_t Stm32Uart::read(std::uint8_t* buffer, std::size_t capacity)
{
    if (!started_ || buffer == nullptr || !config_.rx.connected())
        return 0;
    std::size_t count = 0;
    while (count < capacity && __HAL_UART_GET_FLAG(&handle_, UART_FLAG_RXNE))
        buffer[count++] = static_cast<std::uint8_t>(handle_.Instance->DR & 0xFFU);
    return count;
}

std::size_t Stm32Uart::write(const std::uint8_t* data, std::size_t length)
{
    if (!started_ || data == nullptr || length == 0)
        return 0;
    // HAL_UART_Transmit takes a non-const buffer but does not modify it.
    const HAL_StatusTypeDef result = HAL_UART_Transmit(&handle_, const_cast<std::uint8_t*>(data),
                                                       static_cast<std::uint16_t>(length), config_.write_timeout_ms);
    return result == HAL_OK ? length : 0;
}

}  // namespace brick::platform::stm32::f1
