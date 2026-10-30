#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IBacklight.h"
#include "brick/interfaces/display/ITouchscreen.h"
#include "brick/interfaces/input/IButton.h"

namespace brick::platform::pc
{

/// Single-point touch screen driven by the simulator window or a script.
/// Reports like the hardware drivers: one `pressed` sample, `moved` samples,
/// then one `released` sample at the last position.
class PcTouchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    bool begin() override { return true; }

    /// Updates the finger state. Coordinates are in display pixels.
    void set(bool pressed, std::int16_t x, std::int16_t y)
    {
        using brick::interfaces::display::TouchState;
        if (!pressed && !pressed_)
            return;
        if (!pressed)
            point_.state = TouchState::released;
        else if (!pressed_)
            point_.state = TouchState::pressed;
        else if (!(fresh_ && point_.state == TouchState::pressed))
            point_.state = TouchState::moved;  // an unread press stays a press
        if (pressed)
        {
            point_.x = point_.raw_x = x;
            point_.y = point_.raw_y = y;
        }
        point_.pressure = pressed ? 1 : 0;
        pressed_        = pressed;
        fresh_          = true;
    }

    bool read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override
    {
        count = 0;
        if (!fresh_)
            return false;
        fresh_ = false;
        if (capacity > 0 && points != nullptr)
        {
            points[0] = point_;
            count     = 1;
        }
        return true;
    }

    bool pressed() const { return pressed_; }

private:
    brick::interfaces::display::TouchPoint point_{};
    bool                                   pressed_ = false;
    bool                                   fresh_   = false;
};

/// Push button driven by a keyboard key in the simulator window or a script.
class PcButton final : public brick::interfaces::input::IButton
{
public:
    bool begin() override { return true; }
    bool is_pressed() const override { return pressed_; }
    void set_pressed(bool pressed) { pressed_ = pressed; }

private:
    bool pressed_ = false;
};

/// Backlight whose level dims the simulator window.
class PcBacklight final : public brick::interfaces::display::IBacklight
{
public:
    bool begin(std::uint8_t initial_brightness_percent = 100U)
    {
        return set_brightness_percent(initial_brightness_percent);
    }

    bool set_brightness_percent(std::uint8_t percent) override
    {
        if (percent > 100U)
            return false;
        brightness_ = percent;
        return true;
    }

    std::uint8_t brightness_percent() const override { return brightness_; }

private:
    std::uint8_t brightness_ = 100U;
};

}  // namespace brick::platform::pc
