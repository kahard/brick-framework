#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/display/AssetDescriptor.h"
#include "brick/interfaces/display/IAssetReader.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/WritablePixelBuffer.h"

namespace brick::core::image
{

struct AssetStreamerConfig
{
    std::uint32_t transfer_timeout_ms = 1000;
};

class AssetStreamer final
{
public:
    AssetStreamer(brick::interfaces::display::IDisplayDevice& display, brick::interfaces::display::IAssetReader& reader,
                  AssetStreamerConfig config = {});
    AssetStreamer(brick::interfaces::display::IDisplayDevice& display, AssetStreamerConfig config = {});

    /// Sends the asset to the display in stripes read into scratch. When the
    /// display transfers asynchronously (DisplayCapabilities::async_transfer)
    /// and scratch holds two stripes, its halves alternate: the next stripe is
    /// read while the display still sends the previous one.
    Status stream(const brick::interfaces::display::ImageAsset& asset,
                  brick::interfaces::display::DisplayRect destination, std::uint8_t* scratch,
                  std::size_t scratch_bytes) const;

    /// Copies the asset into destination. Packed rows (stride equal to the
    /// row size) are read straight into destination; scratch is then only the
    /// stripe size and stays untouched. Padded rows go through scratch.
    Status stream_to_buffer(const brick::interfaces::display::ImageAsset&   asset,
                            brick::interfaces::display::WritablePixelBuffer destination, std::uint8_t* scratch,
                            std::size_t scratch_bytes) const;

    Status stream(const brick::interfaces::display::AssetDescriptor& asset,
                  brick::interfaces::display::IAssetSource& source, brick::interfaces::display::DisplayRect destination,
                  std::uint8_t* scratch, std::size_t scratch_bytes) const;

    Status stream_to_buffer(const brick::interfaces::display::AssetDescriptor& asset,
                            brick::interfaces::display::IAssetSource&          source,
                            brick::interfaces::display::WritablePixelBuffer destination, std::uint8_t* scratch,
                            std::size_t scratch_bytes) const;

private:
    /// Reads rows [y, y + height) of the asset into destination, whose rows
    /// are destination_stride bytes apart.
    Status read_stripe(const brick::interfaces::display::ImageAsset& asset, std::uint32_t y, std::uint32_t height,
                       std::uint8_t* destination, std::size_t destination_stride) const;
    Status submit_stripe(const brick::interfaces::display::ImageAsset& asset,
                         brick::interfaces::display::DisplayRect destination, std::uint32_t y, std::uint32_t height,
                         const std::uint8_t* pixels) const;
    /// Streams with scratch split into two stripe buffers used in turn.
    Status stream_pipelined(const brick::interfaces::display::ImageAsset& asset,
                            brick::interfaces::display::DisplayRect destination, std::uint8_t* scratch,
                            std::size_t scratch_bytes) const;

    brick::interfaces::display::IDisplayDevice& display_;
    brick::interfaces::display::IAssetReader*   reader_ = nullptr;
    AssetStreamerConfig                         config_;
};

}  // namespace brick::core::image
