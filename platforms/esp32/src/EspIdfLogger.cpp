#include "brick/platform/esp32/EspIdfLogger.h"
#include <cstdio>
#include "esp_log.h"
namespace brick::platform::esp32
{
void EspIdfLogger::write(brick::interfaces::logging::Level level, const char* tag, const char* format, va_list args)
{
    const int value = static_cast<int>(level);
    if (value < minimum_level_)
        return;
    esp_log_level_t esp_level = ESP_LOG_INFO;
    if (level == brick::interfaces::logging::Level::warning)
        esp_level = ESP_LOG_WARN;
    else if (level == brick::interfaces::logging::Level::error)
        esp_level = ESP_LOG_ERROR;
    else if (level == brick::interfaces::logging::Level::debug)
        esp_level = ESP_LOG_DEBUG;
    char message[512];
    std::vsnprintf(message, sizeof(message), format, args);
    esp_log_write(esp_level, tag, "%s\n", message);
}
}  // namespace brick::platform::esp32
