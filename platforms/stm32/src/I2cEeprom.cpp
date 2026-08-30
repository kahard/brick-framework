#include "brick/platform/stm32/f1/I2cEeprom.h"

#include <algorithm>
#include <cstdint>

namespace brick::platform::stm32::f1
{

I2cEeprom::I2cEeprom(I2cEepromConfig config) : config_(config)
{
}

bool I2cEeprom::read(std::uint32_t address, void* data, std::size_t size)
{
    if (config_.i2c == nullptr || data == nullptr || address >= config_.capacity || size > config_.capacity - address)
        return false;
    return HAL_I2C_Mem_Read(config_.i2c, config_.device_address, static_cast<std::uint16_t>(address), I2C_MEMADD_SIZE_16BIT, static_cast<std::uint8_t*>(data), size, config_.timeout_ms) == HAL_OK;
}

bool I2cEeprom::write(std::uint32_t address, const void* data, std::size_t size)
{
    if (config_.i2c == nullptr || data == nullptr || address >= config_.capacity || size > config_.capacity - address)
        return false;
    auto* bytes = static_cast<const std::uint8_t*>(data);
    while (size != 0)
    {
        const auto page_offset = address % config_.page_size;
        const auto chunk       = std::min(size, static_cast<std::size_t>(config_.page_size - page_offset));
        if (HAL_I2C_Mem_Write(config_.i2c, config_.device_address, static_cast<std::uint16_t>(address), I2C_MEMADD_SIZE_16BIT, const_cast<std::uint8_t*>(bytes), chunk, config_.timeout_ms) != HAL_OK)
            return false;
        if (HAL_I2C_IsDeviceReady(config_.i2c, config_.device_address, 150, config_.timeout_ms) != HAL_OK)
            return false;
        address += static_cast<std::uint32_t>(chunk);
        bytes += chunk;
        size -= chunk;
    }
    return true;
}

}  // namespace brick::platform::stm32::f1
