#pragma once

#include <cstdint>

#include "esp_timer.h"
#include "brick/interfaces/timing/clock.hpp"

namespace brick::platform::esp32 {

class Clock final : public interfaces::timing::IClock {
 public:
  std::uint32_t millis() const override {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
  }
};

}  // namespace brick::platform::esp32
