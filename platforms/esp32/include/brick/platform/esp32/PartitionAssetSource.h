#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/AssetDescriptor.h"
#include "esp_partition.h"

namespace brick::platform::esp32
{

/// Assets in a flash data partition. begin() maps the partition into the
/// address space, so read() is a memory copy through the flash cache: several
/// times faster than esp_partition_read(), which moves at most 64 bytes per
/// SPI command and suspends the caches of both cores meanwhile. When there is
/// no room in the MMU for the mapping, read() falls back to
/// esp_partition_read().
class PartitionAssetSource final : public brick::interfaces::display::IAssetSource
{
public:
    explicit PartitionAssetSource(const char* label) : label_(label) {}
    explicit PartitionAssetSource(const esp_partition_t* partition) : partition_(partition) {}
    ~PartitionAssetSource() override;

    PartitionAssetSource(const PartitionAssetSource&)            = delete;
    PartitionAssetSource& operator=(const PartitionAssetSource&) = delete;

    /// Finds the partition by label (when constructed with one) and maps it.
    Status begin();
    Status read(const brick::interfaces::display::AssetDescriptor& asset, std::size_t offset, std::uint8_t* destination,
                std::size_t bytes) override;

    /// True when reads are served from the memory mapping.
    bool mapped() const { return mapped_ != nullptr; }

private:
    void unmap_();

    const char*                 label_       = nullptr;
    const esp_partition_t*      partition_   = nullptr;
    const std::uint8_t*         mapped_      = nullptr;
    esp_partition_mmap_handle_t mmap_handle_ = 0;
};

}  // namespace brick::platform::esp32
