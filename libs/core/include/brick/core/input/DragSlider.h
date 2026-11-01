#pragma once

#include <cstdint>

#include "brick/interfaces/display/TouchscreenTypes.h"

namespace brick::core::input
{

/// A horizontal position the user drags with a finger, such as the divider
/// of a before/after comparison. A drag moves it by the finger's movement
/// (the finger does not have to start on it); a double tap centres it.
class DragSlider final
{
public:
    /// position() stays within 0..maximum and starts centred. Two taps at
    /// most double_tap_ms apart make a double tap.
    DragSlider(std::uint16_t maximum, std::uint32_t double_tap_ms);

    /// Feeds one touch point read at now_ms. True when position() changed.
    bool update(const interfaces::display::TouchPoint& point, std::uint32_t now_ms);

    std::uint16_t position() const { return position_; }

private:
    bool          tap(std::uint16_t x, std::uint32_t now_ms);
    bool          drag(std::uint16_t x);
    bool          move_to(std::uint16_t position);
    std::uint16_t clamp(int x) const;
    std::uint16_t centre() const;

    std::uint16_t maximum_;
    std::uint32_t double_tap_ms_;
    std::uint16_t position_;
    std::uint16_t last_x_      = 0;
    std::uint32_t last_tap_ms_ = 0;
    bool          dragging_    = false;
    bool          tap_pending_ = false;
};

}  // namespace brick::core::input
