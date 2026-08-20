#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/AssetDescriptor.h"
#include "brick/interfaces/display/ImageAsset.h"

namespace brick::core::image
{

class AssetBundle final
{
public:
    AssetBundle(const std::uint8_t* data, std::size_t data_size,
                const brick::interfaces::display::AssetDescriptor* entries,
                std::size_t entry_count);
    const brick::interfaces::display::AssetDescriptor* find(brick::interfaces::display::AssetId id) const;
    brick::interfaces::display::ImageAsset image(brick::interfaces::display::AssetId id) const;

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t data_size_ = 0;
    const brick::interfaces::display::AssetDescriptor* entries_ = nullptr;
    std::size_t entry_count_ = 0;
};

}  // namespace brick::core::image
