#include "brick/core/image/AssetBundle.h"

namespace brick::core::image
{

AssetBundle::AssetBundle(const std::uint8_t* data, std::size_t data_size,
                         const brick::interfaces::display::AssetDescriptor* entries,
                         std::size_t entry_count)
    : data_(data), data_size_(data_size), entries_(entries), entry_count_(entry_count) {}

const brick::interfaces::display::AssetDescriptor* AssetBundle::find(
    brick::interfaces::display::AssetId id) const
{
    if (entries_ == nullptr) return nullptr;
    for (std::size_t index = 0; index < entry_count_; ++index)
        if (entries_[index].id == id) return &entries_[index];
    return nullptr;
}

brick::interfaces::display::ImageAsset AssetBundle::image(
    brick::interfaces::display::AssetId id) const
{
    const auto* descriptor = find(id);
    if (descriptor == nullptr || data_ == nullptr ||
        static_cast<std::size_t>(descriptor->offset) + descriptor->size > data_size_)
        return {};
    return {data_ + descriptor->offset, descriptor->width, descriptor->height,
            descriptor->stride_bytes, descriptor->size, descriptor->format};
}

}  // namespace brick::core::image
