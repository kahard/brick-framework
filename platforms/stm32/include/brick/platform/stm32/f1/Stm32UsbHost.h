#pragma once

#include <cstdint>

#include "stm32f1xx_hal_hcd.h"
#include "usbh_core.h"

namespace brick::platform::stm32::f1
{

struct Stm32UsbHostConfig
{
    GPIO_TypeDef* power_port = GPIOC;
    std::uint16_t power_pin = GPIO_PIN_10;
    std::uint8_t host_id = 0;
};

class Stm32UsbHost
{
public:
    explicit Stm32UsbHost(Stm32UsbHostConfig config = {}) : config_(config) {}

    bool begin();
    void process();
    bool connected() const { return connected_; }
    bool class_active() const { return class_active_; }
    USBH_HandleTypeDef& handle() { return host_; }
    HCD_HandleTypeDef& hcd() { return hcd_; }
    static Stm32UsbHost* active_;

private:
    static void user_callback_(USBH_HandleTypeDef* host, uint8_t event);
    Stm32UsbHostConfig config_;
    HCD_HandleTypeDef hcd_{};
    USBH_HandleTypeDef host_{};
    bool connected_ = false;
    bool class_active_ = false;
};

}  // namespace brick::platform::stm32::f1
