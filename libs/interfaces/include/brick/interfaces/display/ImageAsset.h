#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/DisplayTypes.h"

namespace brick::interfaces::display
{

struct ImageAsset
{
    const std::uint8_t* data          = nullptr;
    std::uint32_t        width        = 0;
    std::uint32_t        height       = 0;
    std::size_t          stride_bytes = 0;
    std::size_t          data_size    = 0;
    PixelFormat          format       = PixelFormat::rgb565;

    constexpr bool valid() const
    {
        return data != nullptr && width != 0 && height != 0 && stride_bytes != 0 && data_size != 0;
    }
};

}  // namespace brick::interfaces::display
