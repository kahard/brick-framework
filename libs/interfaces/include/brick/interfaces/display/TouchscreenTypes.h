#pragma once

#include <cstddef>
#include <cstdint>

namespace brick::interfaces::display
{

enum class TouchState : std::uint8_t
{
    released,
    pressed,
    moved,
};

struct TouchPoint
{
    std::uint8_t id       = 0;
    std::int16_t x        = 0;
    std::int16_t y        = 0;
    std::int16_t raw_x    = 0;
    std::int16_t raw_y    = 0;
    std::int16_t pressure = 0;
    TouchState   state    = TouchState::released;
};

}  // namespace brick::interfaces::display
