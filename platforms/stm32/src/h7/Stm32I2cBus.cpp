#include "brick/platform/stm32/h7/Stm32I2cBus.h"

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

Status Stm32I2cBus::begin()
{
    if (started_)
        return Status::ok;
    if (config_.instance == nullptr)
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.instance); !is_ok(status))
        return status;
    configure(config_.scl, GPIO_MODE_AF_OD, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW);
    configure(config_.sda, GPIO_MODE_AF_OD, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW);

    handle_.Instance              = config_.instance;
    handle_.Init.Timing           = config_.timing;
    handle_.Init.OwnAddress1      = 0;
    handle_.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    handle_.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    handle_.Init.OwnAddress2      = 0;
    handle_.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    handle_.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    handle_.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    if (const HAL_StatusTypeDef result = HAL_I2C_Init(&handle_); result != HAL_OK)
        return to_status(result);
    if (const HAL_StatusTypeDef result = HAL_I2CEx_ConfigAnalogFilter(&handle_, I2C_ANALOGFILTER_ENABLE);
        result != HAL_OK)
        return to_status(result);
    started_ = true;
    return Status::ok;
}

}  // namespace brick::platform::stm32::h7
