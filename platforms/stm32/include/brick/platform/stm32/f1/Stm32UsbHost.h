#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/platform/stm32/f1/Stm32UsbHostConfig.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_hcd.h"
#include "usbh_core.h"

namespace brick::platform::stm32::f1
{

/// USB OTG FS host (PA11/PA12) with the mass storage class. The application
/// loop calls process() often.
class Stm32UsbHost final
{
public:
    explicit Stm32UsbHost(Stm32UsbHostConfig config) : config_(config) {}

    Status            begin();
    void              process();
    bool              connected() const { return connected_; }
    bool              class_active() const { return class_active_; }
    HOST_StateTypeDef state() const { return host_.gState; }
    bool              port_connected() const;
    bool              storage_ready() const;
    /// Status::not_ready until a mass storage device is enumerated.
    Status               read_blocks(std::uint32_t block, std::uint8_t* data, std::uint32_t count);
    Status               write_blocks(std::uint32_t block, std::uint8_t* data, std::uint32_t count);
    USBH_HandleTypeDef&  handle() { return host_; }
    HCD_HandleTypeDef&   hcd() { return hcd_; }
    static Stm32UsbHost* active_;

private:
    static void user_callback_(USBH_HandleTypeDef* host, uint8_t event);
    void        restart_enumeration_();
    void        recover_after_removal_();
    void        cycle_vbus_();

    Stm32UsbHostConfig config_;
    HCD_HandleTypeDef  hcd_{};
    USBH_HandleTypeDef host_{};
    bool               connected_    = false;
    bool               class_active_ = false;
    bool               port_present_ = false;
    std::uint32_t      attach_tick_  = 0U;
};

}  // namespace brick::platform::stm32::f1
