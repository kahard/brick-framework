#include "brick/core/input/DragSlider.h"

#include <algorithm>

namespace brick::core::input
{

DragSlider::DragSlider(std::uint16_t maximum, std::uint32_t double_tap_ms)
    : maximum_(maximum), double_tap_ms_(double_tap_ms), position_(centre())
{
}

bool DragSlider::update(const interfaces::display::TouchPoint& point, std::uint32_t now_ms)
{
    switch (point.state)
    {
        case interfaces::display::TouchState::pressed:
            return tap(clamp(point.x), now_ms);
        case interfaces::display::TouchState::moved:
            return drag(clamp(point.x));
        case interfaces::display::TouchState::released:
            break;
    }
    dragging_ = false;
    return false;
}

bool DragSlider::tap(std::uint16_t x, std::uint32_t now_ms)
{
    last_x_   = x;
    dragging_ = true;
    // The unsigned difference stays correct when the millisecond counter wraps.
    const bool double_tap = tap_pending_ && now_ms - last_tap_ms_ <= double_tap_ms_;
    tap_pending_          = !double_tap;
    last_tap_ms_          = now_ms;
    return double_tap && move_to(centre());
}

bool DragSlider::drag(std::uint16_t x)
{
    const bool was_dragging = dragging_;
    const int  delta        = x - last_x_;
    last_x_                 = x;
    dragging_               = true;
    return was_dragging && move_to(clamp(position_ + delta));
}

bool DragSlider::move_to(std::uint16_t position)
{
    if (position == position_)
        return false;
    position_ = position;
    return true;
}

std::uint16_t DragSlider::clamp(int x) const
{
    return static_cast<std::uint16_t>(std::clamp(x, 0, static_cast<int>(maximum_)));
}

std::uint16_t DragSlider::centre() const
{
    return static_cast<std::uint16_t>((maximum_ + 1) / 2);
}

}  // namespace brick::core::input
