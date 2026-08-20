#include "brick/core/image/AssetStreamer.h"

namespace brick::core::image
{

AssetStreamer::AssetStreamer(brick::interfaces::display::IDisplayDevice& display,
                             brick::interfaces::display::IAssetReader& reader,
                             AssetStreamerConfig config)
    : display_(display), reader_(reader), config_(config) {}

bool AssetStreamer::stream(const brick::interfaces::display::ImageAsset& asset,
                           brick::interfaces::display::DisplayRect destination,
                           std::uint8_t* scratch, std::size_t scratch_bytes) const
{
    const auto bytes_per_pixel = brick::interfaces::display::pixel_format_bytes(asset.format);
    const auto row_bytes = static_cast<std::size_t>(asset.width) * bytes_per_pixel;
    if (!asset.valid() || scratch == nullptr || destination.empty() ||
        destination.width != static_cast<std::int32_t>(asset.width) ||
        destination.height != static_cast<std::int32_t>(asset.height) ||
        asset.stride_bytes < row_bytes || scratch_bytes < row_bytes || bytes_per_pixel == 0)
        return false;
    const auto stripe_height = static_cast<std::uint32_t>(scratch_bytes / row_bytes);
    if (stripe_height == 0) return false;
    for (std::uint32_t y = 0; y < asset.height; y += stripe_height)
    {
        const auto height = (asset.height - y < stripe_height) ? asset.height - y : stripe_height;
        const auto bytes = static_cast<std::size_t>(height) * row_bytes;
        const auto offset = static_cast<std::size_t>(y) * asset.stride_bytes;
        if (offset + bytes > asset.data_size || !reader_.read(asset, offset, scratch, bytes)) return false;
        const brick::interfaces::display::PixelBuffer buffer{scratch, asset.width, height, row_bytes, asset.format, false};
        if (!display_.submit_buffer({destination.x, destination.y + static_cast<std::int32_t>(y), destination.width, static_cast<std::int32_t>(height)}, buffer) ||
            !display_.wait_for_transfer_complete(config_.transfer_timeout_ms)) return false;
    }
    return true;
}

}  // namespace brick::core::image
