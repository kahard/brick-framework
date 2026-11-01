#include "brick/platform/stm32/f1/Stm32Buses.h"

#include "brick/platform/stm32/f1/HalStatus.h"

namespace brick::platform::stm32::f1
{

Status Stm32I2cBus::begin()
{
    if (config_.instance == nullptr || !config_.scl.connected() || !config_.sda.connected() || config_.clock_hz == 0)
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.instance); !is_ok(status))
        return status;
    configure(config_.scl, GPIO_MODE_AF_OD);
    configure(config_.sda, GPIO_MODE_AF_OD);
    handle_.Instance             = config_.instance;
    handle_.Init.ClockSpeed      = config_.clock_hz;
    handle_.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    handle_.Init.OwnAddress1     = 0;
    handle_.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    handle_.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    handle_.Init.OwnAddress2     = 0;
    handle_.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    handle_.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    return to_status(HAL_I2C_Init(&handle_));
}

Status Stm32SpiBus::begin()
{
    if (config_.instance == nullptr || !config_.sck.connected() || !config_.miso.connected()
        || !config_.mosi.connected())
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.instance); !is_ok(status))
        return status;
    configure(config_.sck, GPIO_MODE_AF_PP);
    configure(config_.mosi, GPIO_MODE_AF_PP);
    configure(config_.miso, GPIO_MODE_INPUT);
    handle_.Instance               = config_.instance;
    handle_.Init.Mode              = SPI_MODE_MASTER;
    handle_.Init.Direction         = SPI_DIRECTION_2LINES;
    handle_.Init.DataSize          = SPI_DATASIZE_8BIT;
    handle_.Init.CLKPolarity       = SPI_POLARITY_LOW;
    handle_.Init.CLKPhase          = SPI_PHASE_1EDGE;
    handle_.Init.NSS               = SPI_NSS_SOFT;
    handle_.Init.BaudRatePrescaler = config_.baud_rate_prescaler;
    handle_.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    handle_.Init.TIMode            = SPI_TIMODE_DISABLE;
    handle_.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    return to_status(HAL_SPI_Init(&handle_));
}

}  // namespace brick::platform::stm32::f1
