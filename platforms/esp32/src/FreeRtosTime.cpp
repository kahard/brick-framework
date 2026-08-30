#include "brick/platform/esp32/FreeRtosTime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32 {

std::uint32_t FreeRtosTime::millis() const {
    return static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void FreeRtosTime::delay_ms(std::uint32_t milliseconds) {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

}  // namespace brick::platform::esp32
