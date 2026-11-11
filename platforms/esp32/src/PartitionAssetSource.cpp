#include "brick/platform/esp32/PartitionAssetSource.h"

#include <cstring>

#include "brick/platform/esp32/EspStatus.h"

namespace brick::platform::esp32
{

PartitionAssetSource::~PartitionAssetSource()
{
    unmap_();
}

Status PartitionAssetSource::begin()
{
    unmap_();
    if (label_ != nullptr)
        partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label_);
    if (partition_ == nullptr)
        return Status::not_found;
    const void* mapped = nullptr;
    if (esp_partition_mmap(partition_, 0, partition_->size, ESP_PARTITION_MMAP_DATA, &mapped, &mmap_handle_) == ESP_OK)
        mapped_ = static_cast<const std::uint8_t*>(mapped);
    // Without a mapping, reads use esp_partition_read(); both are correct.
    return Status::ok;
}

Status PartitionAssetSource::read(const brick::interfaces::display::AssetDescriptor& asset, std::size_t offset,
                                  std::uint8_t* destination, std::size_t bytes)
{
    if (partition_ == nullptr)
        return Status::not_ready;
    // Each subtraction is checked against underflow before it is used.
    const std::size_t partition_size = partition_->size;
    if (destination == nullptr || offset > asset.size || bytes > asset.size - offset || asset.offset > partition_size
        || offset > partition_size - asset.offset || bytes > partition_size - asset.offset - offset)
        return Status::invalid_argument;
    if (mapped_ != nullptr)
    {
        std::memcpy(destination, mapped_ + asset.offset + offset, bytes);
        return Status::ok;
    }
    return to_status(esp_partition_read(partition_, asset.offset + offset, destination, bytes));
}

void PartitionAssetSource::unmap_()
{
    if (mapped_ == nullptr)
        return;
    esp_partition_munmap(mmap_handle_);
    mapped_      = nullptr;
    mmap_handle_ = 0;
}

}  // namespace brick::platform::esp32
