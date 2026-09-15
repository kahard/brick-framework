#include <Arduino.h>

#include <cstdio>

#include "brick/platform/esp8266/ArduinoSerialLogger.h"

namespace brick::platform::esp8266
{

void ArduinoSerialLogger::write(brick::interfaces::logging::Level level, const char* tag, const char* format, va_list args)
{
    if (static_cast<int>(level) < minimum_level_)
        return;

    char message[256] = {};
    std::vsnprintf(message, sizeof(message), format, args);
    Serial.printf("[%s] %s\n", tag == nullptr ? "brick" : tag, message);
}

}  // namespace brick::platform::esp8266
