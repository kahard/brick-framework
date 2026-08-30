#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/AssetDescriptor.h"
#include "esp_partition.h"

namespace brick::platform::esp32
{

class PartitionAssetSource final : public brick::interfaces::display::IAssetSource
{
public:
    explicit PartitionAssetSource(const char* label) : label_(label) {}

    bool begin();
    bool read(const brick::interfaces::display::AssetDescriptor& asset, std::size_t offset,
              std::uint8_t* destination, std::size_t bytes) override;

private:
    const char* label_ = nullptr;
    const esp_partition_t* partition_ = nullptr;
};

}  // namespace brick::platform::esp32
