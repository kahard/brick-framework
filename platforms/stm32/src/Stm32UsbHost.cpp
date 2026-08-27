#include "brick/platform/stm32/f1/Stm32UsbHost.h"

#include "usbh_msc.h"

namespace brick::platform::stm32::f1
{

Stm32UsbHost* Stm32UsbHost::active_ = nullptr;

void Stm32UsbHost::user_callback_(USBH_HandleTypeDef*, uint8_t event)
{
    if (active_ == nullptr)
        return;
    active_->connected_ = event != HOST_USER_DISCONNECTION;
    active_->class_active_ = event == HOST_USER_CLASS_ACTIVE;
}

bool Stm32UsbHost::begin()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef power{};
    power.Pin = config_.power_pin;
    power.Mode = GPIO_MODE_OUTPUT_PP;
    power.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(config_.power_port, &power);
    HAL_GPIO_WritePin(config_.power_port, config_.power_pin, GPIO_PIN_SET);

    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    hcd_.Instance = USB_OTG_FS;
    hcd_.Init.Host_channels = 8;
    hcd_.Init.speed = HCD_SPEED_FULL;
    hcd_.Init.dma_enable = DISABLE;
    hcd_.Init.phy_itface = HCD_PHY_EMBEDDED;
    hcd_.Init.Sof_enable = ENABLE;
    hcd_.Init.low_power_enable = DISABLE;
    hcd_.Init.vbus_sensing_enable = DISABLE;
    hcd_.Init.use_dedicated_ep1 = DISABLE;
    if (HAL_HCD_Init(&hcd_) != HAL_OK)
        return false;

    active_ = this;
    if (USBH_Init(&host_, user_callback_, config_.host_id) != USBH_OK)
        return false;
    host_.pData = &hcd_;
    if (USBH_RegisterClass(&host_, USBH_MSC_CLASS) != USBH_OK)
        return false;
    return USBH_Start(&host_) == USBH_OK;
}

void Stm32UsbHost::process()
{
    if (active_ != nullptr)
        USBH_Process(&host_);
}

bool Stm32UsbHost::storage_ready() const
{
    return class_active_ && USBH_MSC_IsReady(const_cast<USBH_HandleTypeDef*>(&host_)) != 0;
}

bool Stm32UsbHost::read_blocks(std::uint32_t block, std::uint8_t* data, std::uint32_t count)
{
    return storage_ready() && USBH_MSC_Read(&host_, 0, block, data, count) == USBH_OK;
}

bool Stm32UsbHost::write_blocks(std::uint32_t block, std::uint8_t* data, std::uint32_t count)
{
    return storage_ready() && USBH_MSC_Write(&host_, 0, block, data, count) == USBH_OK;
}

}  // namespace brick::platform::stm32::f1

extern "C" void OTG_FS_IRQHandler()
{
    if (brick::platform::stm32::f1::Stm32UsbHost::active_ != nullptr)
        HAL_HCD_IRQHandler(&brick::platform::stm32::f1::Stm32UsbHost::active_->hcd());
}

extern "C" void HAL_HCD_MspInit(HCD_HandleTypeDef* hhcd)
{
    if (hhcd == nullptr || hhcd->Instance != USB_OTG_FS)
        return;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    GPIO_InitTypeDef pins{};
    pins.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    // USB OTG FS D-/D+ use the STM32 alternate-function driver. The legacy
    // F1Boot BSP configures both lines as AF push-pull; plain GPIO input leaves
    // the transceiver disconnected and prevents device enumeration.
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &pins);
}

extern "C" void HAL_HCD_MspDeInit(HCD_HandleTypeDef* hhcd)
{
    if (hhcd != nullptr && hhcd->Instance == USB_OTG_FS)
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
}

extern "C" void HAL_HCD_Connect_Callback(HCD_HandleTypeDef*)
{
    if (brick::platform::stm32::f1::Stm32UsbHost::active_ != nullptr)
        USBH_LL_Connect(&brick::platform::stm32::f1::Stm32UsbHost::active_->handle());
}

extern "C" void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef*)
{
    if (brick::platform::stm32::f1::Stm32UsbHost::active_ != nullptr)
        USBH_LL_Disconnect(&brick::platform::stm32::f1::Stm32UsbHost::active_->handle());
}

extern "C" void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef* hhcd, uint8_t chnum, HCD_URBStateTypeDef state)
{
    (void)chnum;
    (void)hhcd;
}

extern "C" USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef* phost)
{
    (void)phost;
    return USBH_OK;
}

extern "C" USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef*) { return USBH_OK; }
extern "C" USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef* phost)
{
    auto* hcd = static_cast<HCD_HandleTypeDef*>(phost->pData);
    return hcd != nullptr && HAL_HCD_Start(hcd) == HAL_OK ? USBH_OK : USBH_FAIL;
}
extern "C" USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef*) { return USBH_OK; }
extern "C" USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef*) { return USBH_SPEED_FULL; }
extern "C" USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef* phost)
{
    auto* hcd = static_cast<HCD_HandleTypeDef*>(phost->pData);
    return hcd != nullptr && HAL_HCD_ResetPort(hcd) == HAL_OK ? USBH_OK : USBH_FAIL;
}
extern "C" uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef* phost, uint8_t pipe)
{
    auto* hcd = static_cast<HCD_HandleTypeDef*>(phost->pData);
    return hcd == nullptr ? 0u : HAL_HCD_HC_GetXferCount(hcd, pipe);
}
extern "C" USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef* phost, uint8_t pipe, uint8_t epnum, uint8_t dev, uint8_t speed, uint8_t type, uint16_t mps)
{
    auto* hcd = static_cast<HCD_HandleTypeDef*>(phost->pData);
    return hcd != nullptr && HAL_HCD_HC_Init(hcd, pipe, epnum, dev, speed, type, mps) == HAL_OK ? USBH_OK : USBH_FAIL;
}
extern "C" USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef* phost, uint8_t pipe)
{
    auto* hcd = static_cast<HCD_HandleTypeDef*>(phost->pData);
    return hcd != nullptr && HAL_HCD_HC_Halt(hcd, pipe) == HAL_OK ? USBH_OK : USBH_FAIL;
}
extern "C" USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef* phost, uint8_t pipe, uint8_t direction, uint8_t type, uint8_t token, uint8_t* buf, uint16_t len, uint8_t ping)
{
    auto* hcd = static_cast<HCD_HandleTypeDef*>(phost->pData);
    return hcd != nullptr && HAL_HCD_HC_SubmitRequest(hcd, pipe, direction, type, token, buf, len, ping) == HAL_OK ? USBH_OK : USBH_FAIL;
}
extern "C" USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef* phost, uint8_t pipe)
{
    auto* hcd = static_cast<HCD_HandleTypeDef*>(phost->pData);
    return hcd == nullptr ? static_cast<USBH_URBStateTypeDef>(URB_ERROR) : static_cast<USBH_URBStateTypeDef>(HAL_HCD_HC_GetURBState(hcd, pipe));
}
extern "C" USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef*, uint8_t) { return USBH_OK; }
extern "C" USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef*, uint8_t, uint8_t) { return USBH_OK; }
extern "C" uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef*, uint8_t) { return 0; }
extern "C" void USBH_Delay(uint32_t delay) { HAL_Delay(delay); }
