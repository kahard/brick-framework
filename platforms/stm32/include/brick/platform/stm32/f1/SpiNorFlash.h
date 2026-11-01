#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/storage/INorFlash.h"
#include "brick/platform/stm32/f1/Gpio.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct SpiNorFlashConfig
{
    SPI_HandleTypeDef* spi = nullptr;
    GpioPin            chip_select;
    std::uint32_t      timeout_ms = 100;
};

/// Serial NOR flash on an SPI bus the board starts (Stm32SpiBus).
class SpiNorFlash final : public brick::interfaces::storage::INorFlash
{
public:
    explicit SpiNorFlash(SpiNorFlashConfig config) : config_(config) {}

    /// Configures the chip select; the bus must be started already.
    Status begin();
    Status read_id(brick::interfaces::storage::JedecId& id) override;
    Status read(std::uint32_t address, void* data, std::size_t size) override;

private:
    Status transfer_(const std::uint8_t* command, std::size_t command_size, void* data, std::size_t size);

    SpiNorFlashConfig config_;
    bool              started_ = false;
};

}  // namespace brick::platform::stm32::f1
