#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#include "brick/interfaces/time/ITimeProvider.h"
#include "brick/interfaces/timing/IClock.h"

namespace brick::platform::pc
{

/// Monotonic time since construction. With a manual clock (for tests and
/// deterministic headless runs) time only moves through advance_ms() and
/// delay_ms(), and delay_ms() does not sleep.
class PcTime final : public brick::interfaces::time::ITimeProvider, public brick::interfaces::timing::IClock
{
public:
    explicit PcTime(bool manual = false) : manual_(manual), start_(std::chrono::steady_clock::now()) {}

    std::uint32_t millis() const override { return static_cast<std::uint32_t>(micros() / 1000U); }

    std::uint64_t micros() const override
    {
        if (manual_)
            return manual_us_;
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
    }

    void delay_ms(std::uint32_t milliseconds) override
    {
        if (manual_)
            advance_ms(milliseconds);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    void advance_ms(std::uint32_t milliseconds) { manual_us_ += static_cast<std::uint64_t>(milliseconds) * 1000U; }
    bool manual() const { return manual_; }

private:
    bool                                  manual_;
    std::chrono::steady_clock::time_point start_;
    std::uint64_t                         manual_us_ = 0;
};

}  // namespace brick::platform::pc
