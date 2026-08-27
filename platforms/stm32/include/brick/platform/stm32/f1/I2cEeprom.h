#pragma once

#include "brick/interfaces/storage/IByteStorage.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct I2cEepromConfig
{
    I2C_HandleTypeDef* i2c = nullptr;
    std::uint16_t device_address = 0xA2;
    std::uint32_t capacity = 0x10000;
    std::uint16_t page_size = 128;
    std::uint32_t timeout_ms = 500;
};

class I2cEeprom final : public brick::interfaces::storage::IByteStorage
{
public:
    explicit I2cEeprom(I2cEepromConfig config);
    bool read(std::uint32_t address, void* data, std::size_t size) override;
    bool write(std::uint32_t address, const void* data, std::size_t size) override;

private:
    I2cEepromConfig config_;
};

}  // namespace brick::platform::stm32::f1
