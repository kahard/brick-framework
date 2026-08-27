#pragma once

#include <cstddef>
#include <cstdint>

#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct SpiNorFlashConfig
{
    SPI_HandleTypeDef* spi = nullptr;
    GPIO_TypeDef* cs_port = GPIOB;
    std::uint16_t cs_pin = GPIO_PIN_12;
};

class SpiNorFlash
{
public:
    explicit SpiNorFlash(SpiNorFlashConfig config);
    bool read_jedec_id(std::uint8_t id[3]);
    bool read(std::uint32_t address, void* data, std::size_t size);

private:
    bool transfer_(const std::uint8_t* command, std::size_t command_size, void* data, std::size_t size);
    SpiNorFlashConfig config_;
};

}  // namespace brick::platform::stm32::f1
