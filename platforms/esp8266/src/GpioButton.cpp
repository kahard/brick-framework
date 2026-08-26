#include "brick/platform/esp8266/GpioButton.h"

namespace brick::platform::esp8266
{

GpioButton::GpioButton(GpioButtonConfig config) : config_(config)
{
}

bool GpioButton::begin()
{
    pinMode(config_.gpio, config_.pin_mode);
    initialized_ = true;
    return true;
}

bool GpioButton::is_pressed() const
{
    return initialized_ && digitalRead(config_.gpio) == config_.active_level;
}

}  // namespace brick::platform::esp8266
