#include "brick/platform/stm32/f1/SpiNorFlash.h"

#include <array>

#include "brick/platform/stm32/f1/HalStatus.h"

namespace brick::platform::stm32::f1
{

namespace
{

    constexpr std::uint8_t kReadIdCommand   = 0x9F;
    constexpr std::uint8_t kReadDataCommand = 0x03;

}  // namespace

Status SpiNorFlash::begin()
{
    if (config_.spi == nullptr || !config_.chip_select.connected())
        return Status::invalid_argument;
    configure(config_.chip_select, GPIO_MODE_OUTPUT_PP);
    write(config_.chip_select, true);
    started_ = true;
    return Status::ok;
}

Status SpiNorFlash::read_id(brick::interfaces::storage::JedecId& id)
{
    std::array<std::uint8_t, 3> bytes{};
    if (const Status status = transfer_(&kReadIdCommand, 1, bytes.data(), bytes.size()); !is_ok(status))
        return status;
    id = { bytes[0], bytes[1], bytes[2] };
    return Status::ok;
}

Status SpiNorFlash::read(std::uint32_t address, void* data, std::size_t size)
{
    const std::array<std::uint8_t, 4> command = { kReadDataCommand, static_cast<std::uint8_t>(address >> 16),
                                                  static_cast<std::uint8_t>(address >> 8),
                                                  static_cast<std::uint8_t>(address) };
    return transfer_(command.data(), command.size(), data, size);
}

Status SpiNorFlash::transfer_(const std::uint8_t* command, std::size_t command_size, void* data, std::size_t size)
{
    if (!started_)
        return Status::not_ready;
    if (command == nullptr || (data == nullptr && size != 0))
        return Status::invalid_argument;
    write(config_.chip_select, false);
    // HAL_SPI_Transmit takes a non-const buffer but does not modify it.
    HAL_StatusTypeDef result =
        HAL_SPI_Transmit(config_.spi, const_cast<std::uint8_t*>(command), command_size, config_.timeout_ms);
    if (result == HAL_OK && size != 0)
        result = HAL_SPI_Receive(config_.spi, static_cast<std::uint8_t*>(data), size, config_.timeout_ms);
    write(config_.chip_select, true);
    return to_status(result);
}

}  // namespace brick::platform::stm32::f1
