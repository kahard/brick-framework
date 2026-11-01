#include "brick/platform/stm32/f1/SpiNorFlash.h"

#include <array>

namespace brick::platform::stm32::f1
{

SpiNorFlash::SpiNorFlash(SpiNorFlashConfig config) : config_(config)
{
}

Status SpiNorFlash::transfer_(const std::uint8_t* command, std::size_t command_size, void* data, std::size_t size)
{
    if (config_.spi == nullptr || config_.cs_port == nullptr)
        return Status::not_ready;
    if (command == nullptr || (data == nullptr && size != 0))
        return Status::invalid_argument;
    config_.cs_port->BSRR    = static_cast<std::uint32_t>(config_.cs_pin) << 16;  // chip select low
    HAL_StatusTypeDef result = HAL_SPI_Transmit(config_.spi, const_cast<std::uint8_t*>(command), command_size, 100);
    if (result == HAL_OK && size != 0)
        result = HAL_SPI_Receive(config_.spi, static_cast<std::uint8_t*>(data), size, 100);
    config_.cs_port->BSRR = config_.cs_pin;  // chip select high
    return to_status(result);
}

Status SpiNorFlash::read_jedec_id(std::uint8_t id[3])
{
    constexpr std::uint8_t command = 0x9F;
    return transfer_(&command, 1, id, 3);
}

Status SpiNorFlash::read(std::uint32_t address, void* data, std::size_t size)
{
    const std::array<std::uint8_t, 4> command = { 0x03, static_cast<std::uint8_t>(address >> 16),
                                                  static_cast<std::uint8_t>(address >> 8),
                                                  static_cast<std::uint8_t>(address) };
    return transfer_(command.data(), command.size(), data, size);
}

}  // namespace brick::platform::stm32::f1
