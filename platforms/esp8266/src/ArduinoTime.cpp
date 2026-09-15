#include <Arduino.h>

#include "brick/platform/esp8266/ArduinoTime.h"

namespace brick::platform::esp8266
{

std::uint32_t ArduinoTime::millis() const
{
    return ::millis();
}

std::uint64_t ArduinoTime::micros() const
{
    return ::micros();
}

void ArduinoTime::delay_ms(std::uint32_t milliseconds)
{
    ::delay(milliseconds);
}

}  // namespace brick::platform::esp8266
