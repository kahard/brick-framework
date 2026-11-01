#include "brick/core/image/AssetStreamer.h"

#include <cstring>

namespace brick::core::image
{

namespace
{
    class DescriptorReader final : public brick::interfaces::display::IAssetReader
    {
    public:
        DescriptorReader(brick::interfaces::display::IAssetSource&          source,
                         const brick::interfaces::display::AssetDescriptor& descriptor)
            : source_(source), descriptor_(descriptor)
        {
        }

        Status read(const brick::interfaces::display::ImageAsset&, std::size_t offset, std::uint8_t* destination,
                    std::size_t bytes) override
        {
            return source_.read(descriptor_, offset, destination, bytes);
        }

    private:
        brick::interfaces::display::IAssetSource&          source_;
        const brick::interfaces::display::AssetDescriptor& descriptor_;
    };

    brick::interfaces::display::ImageAsset
    descriptor_image(const brick::interfaces::display::AssetDescriptor& descriptor)
    {
        return { reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(1)),
                 descriptor.width,
                 descriptor.height,
                 descriptor.stride_bytes,
                 descriptor.size,
                 descriptor.format };
    }
}  // namespace

AssetStreamer::AssetStreamer(brick::interfaces::display::IDisplayDevice& display,
                             brick::interfaces::display::IAssetReader& reader, AssetStreamerConfig config)
    : display_(display), reader_(&reader), config_(config)
{
}

AssetStreamer::AssetStreamer(brick::interfaces::display::IDisplayDevice& display, AssetStreamerConfig config)
    : display_(display), config_(config)
{
}

Status AssetStreamer::stream(const brick::interfaces::display::ImageAsset& asset,
                             brick::interfaces::display::DisplayRect destination, std::uint8_t* scratch,
                             std::size_t scratch_bytes) const
{
    const auto bytes_per_pixel = brick::interfaces::display::pixel_format_bytes(asset.format);
    const auto row_bytes       = static_cast<std::size_t>(asset.width) * bytes_per_pixel;
    if (!asset.valid() || scratch == nullptr || destination.empty()
        || destination.width != static_cast<std::int32_t>(asset.width)
        || destination.height != static_cast<std::int32_t>(asset.height) || asset.stride_bytes < row_bytes
        || bytes_per_pixel == 0)
        return Status::invalid_argument;
    if (scratch_bytes < row_bytes)
        return Status::no_memory;
    if (reader_ == nullptr)
        return Status::not_ready;

    const auto stripe_height = static_cast<std::uint32_t>(scratch_bytes / row_bytes);

    for (std::uint32_t y = 0; y < asset.height; y += stripe_height)
    {
        const auto height = (asset.height - y < stripe_height) ? asset.height - y : stripe_height;
        const auto bytes  = static_cast<std::size_t>(height) * row_bytes;
        const auto offset = static_cast<std::size_t>(y) * asset.stride_bytes;
        if (offset + bytes > asset.data_size)
            return Status::error;
        if (const Status status = reader_->read(asset, offset, scratch, bytes); !is_ok(status))
            return status;

        const brick::interfaces::display::PixelBuffer buffer{ scratch,   asset.width,  height,
                                                              row_bytes, asset.format, false };
        const brick::interfaces::display::DisplayRect stripe{ destination.x,
                                                              destination.y + static_cast<std::int32_t>(y),
                                                              destination.width, static_cast<std::int32_t>(height) };
        if (const Status status = display_.submit_buffer(stripe, buffer); !is_ok(status))
            return status;
        if (const Status status = display_.wait_for_transfer_complete(config_.transfer_timeout_ms); !is_ok(status))
            return status;
    }
    return Status::ok;
}

Status AssetStreamer::stream_to_buffer(const brick::interfaces::display::ImageAsset&   asset,
                                       brick::interfaces::display::WritablePixelBuffer destination,
                                       std::uint8_t* scratch, std::size_t scratch_bytes) const
{
    const auto bytes_per_pixel = brick::interfaces::display::pixel_format_bytes(asset.format);
    const auto row_bytes       = static_cast<std::size_t>(asset.width) * bytes_per_pixel;
    if (!asset.valid() || !destination.valid() || scratch == nullptr || destination.width != asset.width
        || destination.height != asset.height || destination.format != asset.format
        || destination.stride_bytes < row_bytes || bytes_per_pixel == 0)
        return Status::invalid_argument;
    if (scratch_bytes < row_bytes)
        return Status::no_memory;
    if (reader_ == nullptr)
        return Status::not_ready;

    const auto stripe_height = static_cast<std::uint32_t>(scratch_bytes / row_bytes);

    for (std::uint32_t y = 0; y < asset.height; y += stripe_height)
    {
        const auto height = (asset.height - y < stripe_height) ? asset.height - y : stripe_height;
        const auto bytes  = static_cast<std::size_t>(height) * row_bytes;
        const auto offset = static_cast<std::size_t>(y) * asset.stride_bytes;
        if (offset + bytes > asset.data_size)
            return Status::error;
        if (const Status status = reader_->read(asset, offset, scratch, bytes); !is_ok(status))
            return status;

        for (std::uint32_t row = 0; row < height; ++row)
        {
            std::memcpy(destination.data + static_cast<std::size_t>(y + row) * destination.stride_bytes,
                        scratch + static_cast<std::size_t>(row) * row_bytes, row_bytes);
        }
    }
    return Status::ok;
}

Status AssetStreamer::stream(const brick::interfaces::display::AssetDescriptor& asset,
                             brick::interfaces::display::IAssetSource&          source,
                             brick::interfaces::display::DisplayRect destination, std::uint8_t* scratch,
                             std::size_t scratch_bytes) const
{
    if (!asset.valid())
        return Status::invalid_argument;
    DescriptorReader reader(source, asset);
    AssetStreamer    source_streamer(display_, reader, config_);
    return source_streamer.stream(descriptor_image(asset), destination, scratch, scratch_bytes);
}

Status AssetStreamer::stream_to_buffer(const brick::interfaces::display::AssetDescriptor& asset,
                                       brick::interfaces::display::IAssetSource&          source,
                                       brick::interfaces::display::WritablePixelBuffer    destination,
                                       std::uint8_t* scratch, std::size_t scratch_bytes) const
{
    if (!asset.valid())
        return Status::invalid_argument;
    DescriptorReader reader(source, asset);
    AssetStreamer    source_streamer(display_, reader, config_);
    return source_streamer.stream_to_buffer(descriptor_image(asset), destination, scratch, scratch_bytes);
}

}  // namespace brick::core::image
