#include "brick/core/timing/PeriodicTimer.h"

namespace brick::core::timing
{

PeriodicTimer::PeriodicTimer(const interfaces::timing::IClock& clock) : clock_(clock), last_tick_(clock.millis())
{
}

void PeriodicTimer::reset()
{
    last_tick_ = clock_.millis();
}

bool PeriodicTimer::due(std::uint32_t period_ms)
{
    if (period_ms == 0)
    {
        return false;
    }

    const std::uint32_t now = clock_.millis();
    if (static_cast<std::uint32_t>(now - last_tick_) < period_ms)
    {
        return false;
    }

    last_tick_ = now;
    return true;
}

}  // namespace brick::core::timing
