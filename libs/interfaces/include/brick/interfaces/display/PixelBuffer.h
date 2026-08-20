#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/DisplayTypes.h"

namespace brick::interfaces::display
{

/// Non-owning view of a rectangular pixel buffer.
struct PixelBuffer
{
    const std::uint8_t* data          = nullptr;
    std::uint32_t        width        = 0;
    std::uint32_t        height       = 0;
    std::size_t          stride_bytes = 0;
    PixelFormat          format       = PixelFormat::rgb565;
    bool                 dma_capable  = false;

    constexpr bool valid() const { return data != nullptr && width != 0 && height != 0 && stride_bytes != 0; }
};

}  // namespace brick::interfaces::display
