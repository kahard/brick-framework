#pragma once

#include <cstdint>

#include "brick/interfaces/timing/IClock.h"

namespace brick::core::timing
{

class PeriodicTimer
{
public:
    explicit PeriodicTimer(const interfaces::timing::IClock& clock);

    void reset();
    bool due(std::uint32_t period_ms);

private:
    const interfaces::timing::IClock& clock_;
    std::uint32_t                     last_tick_;
};

}  // namespace brick::core::timing
