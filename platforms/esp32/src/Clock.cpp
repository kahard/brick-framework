#include "brick/platform/esp32/Clock.h"

namespace brick::platform::esp32
{

std::uint32_t Clock::millis() const
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
}

}  // namespace brick::platform::esp32
