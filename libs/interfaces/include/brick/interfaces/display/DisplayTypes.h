#pragma once

#include <cstddef>
#include <cstdint>

namespace brick::interfaces::display
{

struct DisplaySize
{
    std::uint16_t width  = 0;
    std::uint16_t height = 0;
};

enum class PixelFormat : std::uint8_t
{
    rgb565,
    rgb888,
};

constexpr std::size_t pixel_format_bytes(PixelFormat format)
{
    switch (format)
    {
        case PixelFormat::rgb565:
            return 2;
        case PixelFormat::rgb888:
            return 3;
    }
    return 0;
}

enum class Rotation : std::uint8_t
{
    rotate_0,
    rotate_90,
    rotate_180,
    rotate_270,
};

}  // namespace brick::interfaces::display
