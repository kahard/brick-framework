#pragma once

#include <cstdint>
#include "brick/interfaces/time/ITimeProvider.h"

namespace brick::core::time {

class Timer {
public:
    explicit Timer(interfaces::time::ITimeProvider& clock) : clock_(clock) {}
    void start(std::uint32_t duration_ms) { duration_ms_ = duration_ms; deadline_ = clock_.millis() + duration_ms; running_ = true; }
    bool expired() const { return running_ && static_cast<std::int32_t>(clock_.millis() - deadline_) >= 0; }
    void restart() { start(duration_ms_); }
    bool running() const { return running_; }
    void stop() { running_ = false; }

private:
    interfaces::time::ITimeProvider& clock_;
    std::uint32_t duration_ms_ = 0;
    std::uint32_t deadline_ = 0;
    bool running_ = false;
};

}  // namespace brick::core::time
