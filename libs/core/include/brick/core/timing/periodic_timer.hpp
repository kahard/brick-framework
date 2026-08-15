#pragma once

#include <cstdint>

#include "brick/interfaces/timing/clock.hpp"

namespace brick::core::timing {

class PeriodicTimer {
 public:
  explicit PeriodicTimer(const interfaces::timing::IClock& clock)
      : clock_(clock), last_tick_(clock.millis()) {}

  void reset() { last_tick_ = clock_.millis(); }

  bool due(std::uint32_t period_ms) {
    if (period_ms == 0) {
      return false;
    }

    const std::uint32_t now = clock_.millis();
    if (static_cast<std::uint32_t>(now - last_tick_) < period_ms) {
      return false;
    }

    last_tick_ = now;
    return true;
  }

 private:
  const interfaces::timing::IClock& clock_;
  std::uint32_t last_tick_;
};

}  // namespace brick::core::timing
