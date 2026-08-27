#pragma once

#include "brick/interfaces/board/BoardDescriptor.h"
#include "brick/platform/stm32/f1/I2cEeprom.h"
#include "brick/platform/stm32/f1/ResistiveTouchscreen.h"
#include "brick/platform/stm32/f1/SpiNorFlash.h"
#include "brick/platform/stm32/f1/Ssd1963ParallelDisplay.h"

namespace brick::platform::stm32::f1
{

struct St280Pins
{
    GPIO_TypeDef* buzzer_port = GPIOB;
    std::uint16_t buzzer_pin = GPIO_PIN_8;
    GPIO_TypeDef* i2c_port = GPIOB;
    std::uint16_t i2c_scl_pin = GPIO_PIN_6;
    std::uint16_t i2c_sda_pin = GPIO_PIN_7;
    GPIO_TypeDef* spi_flash_cs_port = GPIOB;
    std::uint16_t spi_flash_cs_pin = GPIO_PIN_12;
    std::uint16_t spi_flash_sck_pin = GPIO_PIN_13;
    std::uint16_t spi_flash_miso_pin = GPIO_PIN_14;
    std::uint16_t spi_flash_mosi_pin = GPIO_PIN_15;
};

class St280Board
{
public:
    St280Board(Ssd1963ParallelDisplayConfig display_config,
               ResistiveTouchscreenConfig touch_config,
               I2cEepromConfig eeprom_config,
               SpiNorFlashConfig flash_config)
        : display_(display_config), touch_(touch_config), eeprom_(eeprom_config), flash_(flash_config)
    {
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor()
    {
        using brick::interfaces::board::Capability;
        return {"ST-280", "STM32F105VCT6",
                static_cast<std::uint32_t>(Capability::display) |
                    static_cast<std::uint32_t>(Capability::touchscreen) |
                    static_cast<std::uint32_t>(Capability::eeprom) |
                    static_cast<std::uint32_t>(Capability::spi_flash) |
                    static_cast<std::uint32_t>(Capability::buzzer)};
    }

    static constexpr St280Pins pins() { return {}; }

    bool begin()
    {
        return display_.begin() && touch_.begin();
    }

    Ssd1963ParallelDisplay& display() { return display_; }
    ResistiveTouchscreen& touch() { return touch_; }
    I2cEeprom& eeprom() { return eeprom_; }
    SpiNorFlash& flash() { return flash_; }

private:
    Ssd1963ParallelDisplay display_;
    ResistiveTouchscreen touch_;
    I2cEeprom eeprom_;
    SpiNorFlash flash_;
};

}  // namespace brick::platform::stm32::f1
