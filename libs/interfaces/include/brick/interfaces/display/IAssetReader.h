#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/ImageAsset.h"

namespace brick::interfaces::display
{

class IAssetReader
{
public:
    virtual ~IAssetReader() = default;
    virtual bool read(const ImageAsset& asset, std::size_t offset, std::uint8_t* destination, std::size_t bytes) = 0;
};

}  // namespace brick::interfaces::display
