#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/display/DisplayRect.h"
#include "brick/interfaces/display/PixelBuffer.h"
#include "brick/interfaces/display/WritablePixelBuffer.h"

namespace brick::interfaces::display
{

/// Optional contract for displays exposing their scan-out frame buffers.
class IFrameBufferDisplay
{
public:
    virtual ~IFrameBufferDisplay() = default;

    virtual std::uint8_t frame_buffer_count() const                                        = 0;
    virtual Status       get_frame_buffer(std::uint8_t index, WritablePixelBuffer& buffer) = 0;
    virtual Status       present_frame_buffer(std::uint8_t index)                          = 0;

    /// Copies pixels (width x height of area, any memory) into area of frame
    /// buffer index, which may be the one off the screen; a DMA engine does
    /// it where the display has one. Like IDisplayDevice::submit_buffer() it
    /// may return before the copy ends: pixels stay untouched until the
    /// display's wait_for_transfer_complete() returns (or the next copy or
    /// transfer starts, which waits for this one). not_supported when the
    /// display cannot (the default).
    virtual Status copy_to_frame_buffer(std::uint8_t index, DisplayRect area, const PixelBuffer& pixels)
    {
        (void)index;
        (void)area;
        (void)pixels;
        return Status::not_supported;
    }

    /// Copies area of frame buffer from into the same area of frame buffer
    /// to, so that both hold the same image there. Like
    /// copy_to_frame_buffer() it may return before the copy ends; copies
    /// into one frame buffer land in the order they were made.
    virtual Status copy_between_frame_buffers(std::uint8_t from, std::uint8_t to, DisplayRect area)
    {
        (void)from;
        (void)to;
        (void)area;
        return Status::not_supported;
    }
};

}  // namespace brick::interfaces::display
