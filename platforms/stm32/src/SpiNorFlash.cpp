#include "brick/platform/stm32/f1/SpiNorFlash.h"

#include <array>

namespace brick::platform::stm32::f1
{

SpiNorFlash::SpiNorFlash(SpiNorFlashConfig config) : config_(config) {}

bool SpiNorFlash::transfer_(const std::uint8_t* command, std::size_t command_size, void* data, std::size_t size)
{
    if (config_.spi == nullptr || command == nullptr || (data == nullptr && size != 0))
        return false;
    config_.cs_port->BSRR = static_cast<std::uint32_t>(config_.cs_pin) << 16;
    const auto command_ok = HAL_SPI_Transmit(config_.spi, const_cast<std::uint8_t*>(command), command_size, 100) == HAL_OK;
    const auto data_ok = command_ok && (size == 0 || HAL_SPI_Receive(config_.spi, static_cast<std::uint8_t*>(data), size, 100) == HAL_OK);
    config_.cs_port->BSRR = config_.cs_pin;
    return data_ok;
}

bool SpiNorFlash::read_jedec_id(std::uint8_t id[3])
{
    constexpr std::uint8_t command = 0x9F;
    return transfer_(&command, 1, id, 3);
}

bool SpiNorFlash::read(std::uint32_t address, void* data, std::size_t size)
{
    const std::array<std::uint8_t, 4> command = {
        0x03, static_cast<std::uint8_t>(address >> 16), static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address)};
    return transfer_(command.data(), command.size(), data, size);
}

}  // namespace brick::platform::stm32::f1
