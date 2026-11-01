#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/audio/IBuzzer.h"

namespace brick::core::audio
{

/// A number of beeps played without blocking: start() them, then call
/// update() from the loop. Used to signal a result by a count of beeps.
class BeepSequence final
{
public:
    explicit BeepSequence(brick::interfaces::audio::IBuzzer& buzzer, std::uint16_t on_ms = 80,
                          std::uint16_t off_ms = 120)
        : buzzer_(buzzer), on_ms_(on_ms), off_ms_(off_ms)
    {
    }

    /// Starts count beeps, replacing a sequence still playing. A non-zero
    /// repeat_pause_ms plays them again after that pause, until the next start().
    Status start(std::uint8_t count, std::uint32_t now_ms, std::uint32_t repeat_pause_ms = 0);
    /// Switches the buzzer when the current beep or pause is over.
    Status update(std::uint32_t now_ms);
    bool   active() const { return remaining_ != 0; }

private:
    Status switch_to_(bool on, std::uint32_t switch_ms);

    brick::interfaces::audio::IBuzzer& buzzer_;
    std::uint16_t                      on_ms_;
    std::uint16_t                      off_ms_;
    std::uint32_t                      repeat_pause_ms_ = 0;
    std::uint8_t                       count_           = 0;
    std::uint8_t                       remaining_       = 0;
    bool                               on_              = false;
    std::uint32_t                      switch_ms_       = 0;
};

}  // namespace brick::core::audio
