#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/DisplayTypes.h"

namespace brick::interfaces::display
{

using AssetId = std::uint32_t;

struct AssetDescriptor
{
    AssetId id = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t stride_bytes = 0;
    PixelFormat format = PixelFormat::rgb565;
    constexpr bool valid() const { return id != 0 && size != 0 && width != 0 && height != 0 && stride_bytes != 0; }
};

class IAssetSource
{
public:
    virtual ~IAssetSource() = default;
    virtual bool read(const AssetDescriptor& asset, std::size_t offset,
                      std::uint8_t* destination, std::size_t bytes) = 0;
};

}  // namespace brick::interfaces::display
