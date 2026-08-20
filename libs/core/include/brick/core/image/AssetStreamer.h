#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IAssetReader.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/WritablePixelBuffer.h"

namespace brick::core::image
{

struct AssetStreamerConfig { std::uint32_t transfer_timeout_ms = 1000; };

class AssetStreamer final
{
public:
    AssetStreamer(brick::interfaces::display::IDisplayDevice& display,
                  brick::interfaces::display::IAssetReader& reader,
                  AssetStreamerConfig config = {});
    bool stream(const brick::interfaces::display::ImageAsset& asset,
                brick::interfaces::display::DisplayRect destination,
                std::uint8_t* scratch, std::size_t scratch_bytes) const;
    bool stream_to_buffer(const brick::interfaces::display::ImageAsset& asset,
                          brick::interfaces::display::WritablePixelBuffer destination,
                          std::uint8_t* scratch, std::size_t scratch_bytes) const;

private:
    brick::interfaces::display::IDisplayDevice& display_;
    brick::interfaces::display::IAssetReader& reader_;
    AssetStreamerConfig config_;
};

}  // namespace brick::core::image
