#include "brick/core/audio/BeepSequence.h"

namespace brick::core::audio
{

Status BeepSequence::start(std::uint8_t count, std::uint32_t now_ms, std::uint32_t repeat_pause_ms)
{
    count_           = count;
    remaining_       = count;
    repeat_pause_ms_ = repeat_pause_ms;
    return switch_to_(count != 0, now_ms + on_ms_);
}

Status BeepSequence::update(std::uint32_t now_ms)
{
    // Signed difference: correct across a wrap of the millisecond counter.
    if (remaining_ == 0 || static_cast<std::int32_t>(now_ms - switch_ms_) < 0)
        return Status::ok;
    if (!on_)
        return switch_to_(true, now_ms + on_ms_);
    if (--remaining_ == 0 && repeat_pause_ms_ != 0)
    {
        remaining_ = count_;
        return switch_to_(false, now_ms + repeat_pause_ms_);
    }
    return switch_to_(false, now_ms + off_ms_);
}

Status BeepSequence::switch_to_(bool on, std::uint32_t switch_ms)
{
    on_                 = on;
    switch_ms_          = switch_ms;
    const Status status = buzzer_.set_enabled(on);
    if (!is_ok(status))
        remaining_ = 0;
    return status;
}

}  // namespace brick::core::audio
