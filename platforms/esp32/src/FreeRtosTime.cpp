#include "brick/platform/esp32/FreeRtosTime.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32
{

std::uint32_t FreeRtosTime::millis() const
{
    // From the microsecond timer, not the tick count: exact at any
    // CONFIG_FREERTOS_HZ (ticks are 10 ms at the default 100 Hz).
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

std::uint64_t FreeRtosTime::micros() const
{
    return static_cast<std::uint64_t>(esp_timer_get_time());
}

void FreeRtosTime::delay_ms(std::uint32_t milliseconds)
{
    // pdMS_TO_TICKS() rounds down: at 100 Hz, 1-9 ms is 0 ticks, and
    // vTaskDelay(0) only yields to tasks of the same priority, so a loop with
    // delay_ms(1) keeps the core busy and starves the idle task (task
    // watchdog). Any non-zero delay sleeps for at least one tick.
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    if (ticks == 0 && milliseconds > 0)
        ticks = 1;
    vTaskDelay(ticks);
}

}  // namespace brick::platform::esp32
