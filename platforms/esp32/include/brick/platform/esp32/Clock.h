#pragma once

#include <cstdint>

#include "brick/interfaces/timing/IClock.h"
#include "esp_timer.h"

namespace brick::platform::esp32
{

class Clock final : public interfaces::timing::IClock
{
public:
    std::uint32_t millis() const override;
};

}  // namespace brick::platform::esp32
