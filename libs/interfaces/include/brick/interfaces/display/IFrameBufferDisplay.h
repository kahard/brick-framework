#pragma once

#include <cstdint>

#include "brick/Status.h"
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
};

}  // namespace brick::interfaces::display
