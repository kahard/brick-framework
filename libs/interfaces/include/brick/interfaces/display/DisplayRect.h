#pragma once

#include <cstdint>

namespace brick::interfaces::display
{

struct DisplayRect
{
    std::int32_t x      = 0;
    std::int32_t y      = 0;
    std::int32_t width  = 0;
    std::int32_t height = 0;

    constexpr bool empty() const { return width <= 0 || height <= 0; }

    constexpr bool contains(DisplayRect other) const { return other.x >= x && other.y >= y && other.x + other.width <= x + width && other.y + other.height <= y + height; }
};

}  // namespace brick::interfaces::display
