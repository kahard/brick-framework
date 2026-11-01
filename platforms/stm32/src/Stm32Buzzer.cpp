#include "brick/platform/stm32/f1/Stm32Buzzer.h"

namespace brick::platform::stm32::f1
{

Status Stm32Buzzer::begin()
{
    if (!config_.pin.connected())
        return Status::invalid_argument;
    configure(config_.pin, GPIO_MODE_OUTPUT_PP);
    initialized_ = true;
    return set_enabled(false);
}

Status Stm32Buzzer::set_enabled(bool enabled)
{
    if (!initialized_)
        return Status::not_ready;
    write(config_.pin, enabled == config_.active_high);
    return Status::ok;
}

Status Stm32Buzzer::beep(std::uint16_t duration_ms)
{
    if (const Status status = set_enabled(true); !is_ok(status))
        return status;
    HAL_Delay(duration_ms);
    return set_enabled(false);
}

}  // namespace brick::platform::stm32::f1
