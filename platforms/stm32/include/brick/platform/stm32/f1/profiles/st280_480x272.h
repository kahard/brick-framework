#pragma once

#include "brick/platform/stm32/f1/ResistiveTouchscreen.h"
#include "brick/platform/stm32/f1/Ssd1963ParallelDisplay.h"
#include "brick/platform/stm32/f1/St280Board.h"

namespace brick::platform::stm32::f1::profiles
{

inline Ssd1963ParallelDisplayConfig st280_480x272()
{
    return {};
}

inline ResistiveTouchscreenConfig st280_resistive_touch()
{
    return {};
}

inline St280Board st280_board(I2C_HandleTypeDef* i2c, SPI_HandleTypeDef* spi)
{
    I2cEepromConfig eeprom{};
    eeprom.i2c = i2c;
    SpiNorFlashConfig flash{};
    flash.spi = spi;
    return St280Board(st280_480x272(), st280_resistive_touch(), eeprom, flash);
}

}  // namespace brick::platform::stm32::f1::profiles
