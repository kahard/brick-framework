#pragma once

#include "brick/interfaces/board/BoardDescriptor.h"
#include "brick/platform/stm32/f1/I2cEeprom.h"
#include "brick/platform/stm32/f1/ResistiveTouchscreen.h"
#include "brick/platform/stm32/f1/SpiNorFlash.h"
#include "brick/platform/stm32/f1/Ssd1963ParallelDisplay.h"
#include "brick/platform/stm32/f1/Stm32Buzzer.h"
#include "brick/platform/stm32/f1/Stm32PwmBacklight.h"

namespace brick::platform::stm32::f1
{

struct St280Pins
{
    GPIO_TypeDef* buzzer_port = GPIOB;
    std::uint16_t buzzer_pin = GPIO_PIN_8;
    GPIO_TypeDef* backlight_port = GPIOC;
    std::uint16_t backlight_pin = GPIO_PIN_8;
    GPIO_TypeDef* usb_power_port = GPIOC;
    std::uint16_t usb_power_pin = GPIO_PIN_10;
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
    St280Board()
        : display_(Ssd1963ParallelDisplayConfig{}), touch_(ResistiveTouchscreenConfig{}), eeprom_(I2cEepromConfig{&i2c_}), flash_(SpiNorFlashConfig{&spi_}), buzzer_(Stm32BuzzerConfig{}), backlight_(Stm32PwmBacklightConfig{&backlight_timer_})
    {
    }

    St280Board(Ssd1963ParallelDisplayConfig display_config,
               ResistiveTouchscreenConfig touch_config,
               I2cEepromConfig eeprom_config,
               SpiNorFlashConfig flash_config)
        : display_(display_config), touch_(touch_config), eeprom_(eeprom_config), flash_(flash_config), buzzer_(Stm32BuzzerConfig{}), backlight_(Stm32PwmBacklightConfig{&backlight_timer_})
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
                    static_cast<std::uint32_t>(Capability::buzzer) |
                    static_cast<std::uint32_t>(Capability::backlight) |
                    static_cast<std::uint32_t>(Capability::usb_host)};
    }

    static constexpr St280Pins pins() { return {}; }

    bool begin()
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        if (!init_i2c_() || !init_spi_())
            return false;
        return buzzer_.begin() && backlight_.begin() && display_.begin() && touch_.begin();
    }

    Ssd1963ParallelDisplay& display() { return display_; }
    ResistiveTouchscreen& touch() { return touch_; }
    I2cEeprom& eeprom() { return eeprom_; }
    SpiNorFlash& flash() { return flash_; }
    I2C_HandleTypeDef& i2c() { return i2c_; }
    SPI_HandleTypeDef& spi() { return spi_; }
    Stm32Buzzer& buzzer() { return buzzer_; }
    Stm32PwmBacklight& backlight() { return backlight_; }

private:
    bool init_i2c_()
    {
        __HAL_RCC_I2C1_CLK_ENABLE();
        GPIO_InitTypeDef pins{};
        pins.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        pins.Mode = GPIO_MODE_AF_OD;
        pins.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &pins);
        i2c_.Instance = I2C1;
        i2c_.Init.ClockSpeed = 50000;
        i2c_.Init.DutyCycle = I2C_DUTYCYCLE_2;
        i2c_.Init.OwnAddress1 = 0;
        i2c_.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
        i2c_.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
        i2c_.Init.OwnAddress2 = 0;
        i2c_.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
        i2c_.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
        return HAL_I2C_Init(&i2c_) == HAL_OK;
    }

    bool init_spi_()
    {
        __HAL_RCC_SPI2_CLK_ENABLE();
        GPIO_InitTypeDef pins{};
        pins.Pin = GPIO_PIN_13 | GPIO_PIN_15;
        pins.Mode = GPIO_MODE_AF_PP;
        pins.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &pins);
        pins.Pin = GPIO_PIN_14;
        pins.Mode = GPIO_MODE_INPUT;
        pins.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOB, &pins);
        pins.Pin = GPIO_PIN_12;
        pins.Mode = GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_Init(GPIOB, &pins);
        GPIOB->BSRR = GPIO_PIN_12;
        spi_.Instance = SPI2;
        spi_.Init.Mode = SPI_MODE_MASTER;
        spi_.Init.Direction = SPI_DIRECTION_2LINES;
        spi_.Init.DataSize = SPI_DATASIZE_8BIT;
        spi_.Init.CLKPolarity = SPI_POLARITY_LOW;
        spi_.Init.CLKPhase = SPI_PHASE_1EDGE;
        spi_.Init.NSS = SPI_NSS_SOFT;
        spi_.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
        spi_.Init.FirstBit = SPI_FIRSTBIT_MSB;
        spi_.Init.TIMode = SPI_TIMODE_DISABLE;
        spi_.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
        return HAL_SPI_Init(&spi_) == HAL_OK;
    }

    I2C_HandleTypeDef i2c_{};
    SPI_HandleTypeDef spi_{};
    TIM_HandleTypeDef backlight_timer_{};
    Ssd1963ParallelDisplay display_;
    ResistiveTouchscreen touch_;
    I2cEeprom eeprom_;
    SpiNorFlash flash_;
    Stm32Buzzer buzzer_;
    Stm32PwmBacklight backlight_;
};

}  // namespace brick::platform::stm32::f1
