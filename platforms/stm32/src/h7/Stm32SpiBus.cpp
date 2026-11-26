#include "brick/platform/stm32/h7/Stm32SpiBus.h"

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

Status Stm32SpiBus::begin()
{
    if (started_)
        return Status::ok;
    if (config_.instance == nullptr || !config_.sck.pin.connected() || !config_.miso.pin.connected()
        || !config_.mosi.pin.connected())
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.instance); !is_ok(status))
        return status;
    configure(config_.sck, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH);
    configure(config_.mosi, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH);
    configure(config_.miso, GPIO_MODE_AF_PP, GPIO_PULLUP, GPIO_SPEED_FREQ_HIGH);

    handle_.Instance                     = config_.instance;
    handle_.Init.Mode                    = SPI_MODE_MASTER;
    handle_.Init.Direction               = SPI_DIRECTION_2LINES;
    handle_.Init.DataSize                = SPI_DATASIZE_8BIT;
    handle_.Init.CLKPolarity             = SPI_POLARITY_LOW;
    handle_.Init.CLKPhase                = SPI_PHASE_1EDGE;
    handle_.Init.NSS                     = SPI_NSS_SOFT;
    handle_.Init.BaudRatePrescaler       = config_.baud_rate_prescaler;
    handle_.Init.FirstBit                = SPI_FIRSTBIT_MSB;
    handle_.Init.TIMode                  = SPI_TIMODE_DISABLE;
    handle_.Init.CRCCalculation          = SPI_CRCCALCULATION_DISABLE;
    handle_.Init.CRCPolynomial           = 7;
    handle_.Init.NSSPMode                = SPI_NSS_PULSE_DISABLE;
    handle_.Init.NSSPolarity             = SPI_NSS_POLARITY_LOW;
    handle_.Init.FifoThreshold           = SPI_FIFO_THRESHOLD_01DATA;
    handle_.Init.MasterSSIdleness        = SPI_MASTER_SS_IDLENESS_00CYCLE;
    handle_.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    handle_.Init.MasterReceiverAutoSusp  = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    handle_.Init.MasterKeepIOState       = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    handle_.Init.IOSwap                  = SPI_IO_SWAP_DISABLE;
    if (const HAL_StatusTypeDef result = HAL_SPI_Init(&handle_); result != HAL_OK)
        return to_status(result);
    started_ = true;
    return Status::ok;
}

}  // namespace brick::platform::stm32::h7
