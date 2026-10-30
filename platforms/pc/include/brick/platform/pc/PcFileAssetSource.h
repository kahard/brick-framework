#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "brick/interfaces/display/AssetDescriptor.h"

namespace brick::platform::pc
{

/// Asset bundle read from a host file at begin(); stands in for the flash
/// partition the embedded build writes the same bundle to.
class PcFileAssetSource final : public brick::interfaces::display::IAssetSource
{
public:
    explicit PcFileAssetSource(std::string path) : path_(std::move(path)) {}

    bool begin()
    {
        std::ifstream file(path_, std::ios::binary);
        if (!file)
            return false;
        data_.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !data_.empty();
    }

    bool read(const brick::interfaces::display::AssetDescriptor& asset, std::size_t offset, std::uint8_t* destination,
              std::size_t bytes) override
    {
        if (destination == nullptr || offset > asset.size || bytes > asset.size - offset || asset.offset > data_.size()
            || asset.offset + offset + bytes > data_.size())
            return false;
        std::memcpy(destination, data_.data() + asset.offset + offset, bytes);
        return true;
    }

    std::size_t size() const { return data_.size(); }

private:
    std::string       path_;
    std::vector<char> data_;
};

}  // namespace brick::platform::pc
