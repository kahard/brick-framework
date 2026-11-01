#include "brick/platform/stm32/f1/I2cEeprom.h"

#include <algorithm>

#include "brick/platform/stm32/f1/HalStatus.h"

namespace brick::platform::stm32::f1
{

namespace
{

    // Attempts to address the EEPROM while it finishes writing a page.
    constexpr std::uint32_t kWriteCycleTrials = 150;

}  // namespace

Status I2cEeprom::begin()
{
    if (config_.i2c == nullptr || config_.capacity == 0 || config_.page_size == 0)
        return Status::invalid_argument;
    started_ = true;
    return Status::ok;
}

Status I2cEeprom::read(std::uint32_t address, void* data, std::size_t size)
{
    if (const Status status = check_range_(address, data, size); !is_ok(status))
        return status;
    return to_status(HAL_I2C_Mem_Read(config_.i2c, config_.device_address, static_cast<std::uint16_t>(address),
                                      I2C_MEMADD_SIZE_16BIT, static_cast<std::uint8_t*>(data), size,
                                      config_.timeout_ms));
}

Status I2cEeprom::write(std::uint32_t address, const void* data, std::size_t size)
{
    if (const Status status = check_range_(address, data, size); !is_ok(status))
        return status;
    auto* bytes = static_cast<const std::uint8_t*>(data);
    while (size != 0)
    {
        const auto page_offset = address % config_.page_size;
        const auto chunk       = std::min(size, static_cast<std::size_t>(config_.page_size - page_offset));
        // HAL_I2C_Mem_Write takes a non-const buffer but does not modify it.
        if (const HAL_StatusTypeDef result =
                HAL_I2C_Mem_Write(config_.i2c, config_.device_address, static_cast<std::uint16_t>(address),
                                  I2C_MEMADD_SIZE_16BIT, const_cast<std::uint8_t*>(bytes), chunk, config_.timeout_ms);
            result != HAL_OK)
            return to_status(result);
        // The EEPROM does not acknowledge while it writes the page.
        if (const HAL_StatusTypeDef result =
                HAL_I2C_IsDeviceReady(config_.i2c, config_.device_address, kWriteCycleTrials, config_.timeout_ms);
            result != HAL_OK)
            return to_status(result);
        address += static_cast<std::uint32_t>(chunk);
        bytes += chunk;
        size -= chunk;
    }
    return Status::ok;
}

Status I2cEeprom::check_range_(std::uint32_t address, const void* data, std::size_t size) const
{
    if (!started_)
        return Status::not_ready;
    if (data == nullptr || address >= config_.capacity || size > config_.capacity - address)
        return Status::invalid_argument;
    return Status::ok;
}

}  // namespace brick::platform::stm32::f1
