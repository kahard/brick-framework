#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/storage/IByteStorage.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

/// A 24-series EEPROM with 16-bit memory addresses (24C32 ... 24C512).
struct I2cEepromConfig
{
    I2C_HandleTypeDef* i2c = nullptr;
    /// 8-bit address as HAL expects it: 0xA0 with A0..A2 low.
    std::uint16_t device_address = 0xA0;
    std::uint32_t capacity       = 0;
    std::uint16_t page_size      = 0;
    std::uint32_t timeout_ms     = 500;
};

/// EEPROM on an I2C bus the board starts (Stm32I2cBus).
class I2cEeprom final : public brick::interfaces::storage::IByteStorage
{
public:
    explicit I2cEeprom(I2cEepromConfig config) : config_(config) {}

    /// Checks the configuration; the bus must be started already.
    Status begin();
    Status read(std::uint32_t address, void* data, std::size_t size) override;
    Status write(std::uint32_t address, const void* data, std::size_t size) override;

private:
    Status check_range_(std::uint32_t address, const void* data, std::size_t size) const;

    I2cEepromConfig config_;
    bool            started_ = false;
};

}  // namespace brick::platform::stm32::f1
