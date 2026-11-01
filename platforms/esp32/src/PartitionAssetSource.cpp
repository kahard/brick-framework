#include "brick/platform/esp32/PartitionAssetSource.h"

#include "brick/platform/esp32/EspStatus.h"

namespace brick::platform::esp32
{

Status PartitionAssetSource::begin()
{
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label_);
    return partition_ != nullptr ? Status::ok : Status::not_found;
}

Status PartitionAssetSource::read(const brick::interfaces::display::AssetDescriptor& asset, std::size_t offset,
                                  std::uint8_t* destination, std::size_t bytes)
{
    if (partition_ == nullptr)
        return Status::not_ready;
    if (destination == nullptr || offset > asset.size || bytes > asset.size - offset || asset.offset > partition_->size
        || bytes > partition_->size - asset.offset - offset)
        return Status::invalid_argument;
    return to_status(esp_partition_read(partition_, asset.offset + offset, destination, bytes));
}

}  // namespace brick::platform::esp32
