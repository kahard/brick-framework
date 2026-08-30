#include "brick/platform/esp32/PartitionAssetSource.h"

namespace brick::platform::esp32
{

bool PartitionAssetSource::begin()
{
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label_);
    return partition_ != nullptr;
}

bool PartitionAssetSource::read(const brick::interfaces::display::AssetDescriptor& asset, std::size_t offset,
                                std::uint8_t* destination, std::size_t bytes)
{
    if (partition_ == nullptr || destination == nullptr || offset > asset.size || bytes > asset.size - offset ||
        asset.offset > partition_->size || bytes > partition_->size - asset.offset - offset)
        return false;
    return esp_partition_read(partition_, asset.offset + offset, destination, bytes) == ESP_OK;
}

}  // namespace brick::platform::esp32
