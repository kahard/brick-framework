#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "brick/Status.h"
#include "brick/interfaces/display/DisplayRect.h"
#include "brick/interfaces/display/PixelBuffer.h"
#include "brick/interfaces/display/WritablePixelBuffer.h"

namespace brick::core::display
{

/// CPU copies for IFrameBufferDisplay::copy_to_frame_buffer() and
/// copy_between_frame_buffers() on displays without a DMA engine for them
/// (and as their fallback). RGB565, row by row with memcpy.

/// True when area lies inside a width x height frame.
constexpr bool area_inside(brick::interfaces::display::DisplayRect area, std::uint32_t width, std::uint32_t height)
{
    return !area.empty() && area.x >= 0 && area.y >= 0
           && static_cast<std::uint32_t>(area.x) + static_cast<std::uint32_t>(area.width) <= width
           && static_cast<std::uint32_t>(area.y) + static_cast<std::uint32_t>(area.height) <= height;
}

/// pixels (area.width x area.height) into area of frame.
inline Status copy_into_frame(const brick::interfaces::display::WritablePixelBuffer& frame,
                              brick::interfaces::display::DisplayRect                area,
                              const brick::interfaces::display::PixelBuffer&         pixels)
{
    constexpr std::size_t kBytesPerPixel = 2;
    if (!frame.valid() || !pixels.valid() || !area_inside(area, frame.width, frame.height)
        || pixels.width != static_cast<std::uint32_t>(area.width)
        || pixels.height != static_cast<std::uint32_t>(area.height) || pixels.format != frame.format)
        return Status::invalid_argument;
    const std::size_t row_bytes = static_cast<std::size_t>(area.width) * kBytesPerPixel;
    std::uint8_t*     to        = frame.data + static_cast<std::size_t>(area.y) * frame.stride_bytes
                       + static_cast<std::size_t>(area.x) * kBytesPerPixel;
    const std::uint8_t* from = pixels.data;
    for (std::int32_t row = 0; row < area.height; ++row, to += frame.stride_bytes, from += pixels.stride_bytes)
        std::memcpy(to, from, row_bytes);
    return Status::ok;
}

/// area of from into the same area of to; both frames have the same size.
inline Status copy_frame_area(const brick::interfaces::display::WritablePixelBuffer& from,
                              const brick::interfaces::display::WritablePixelBuffer& to,
                              brick::interfaces::display::DisplayRect                area)
{
    constexpr std::size_t kBytesPerPixel = 2;
    if (!from.valid() || from.width != to.width || from.height != to.height
        || !area_inside(area, from.width, from.height))
        return Status::invalid_argument;
    const brick::interfaces::display::PixelBuffer source{
        from.data + static_cast<std::size_t>(area.y) * from.stride_bytes
            + static_cast<std::size_t>(area.x) * kBytesPerPixel,
        static_cast<std::uint32_t>(area.width),
        static_cast<std::uint32_t>(area.height),
        from.stride_bytes,
        from.format,
        false,
    };
    return copy_into_frame(to, area, source);
}

}  // namespace brick::core::display
