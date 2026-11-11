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

    std::size_t row_bytes_of(const brick::interfaces::display::ImageAsset& asset)
    {
        return static_cast<std::size_t>(asset.width) * brick::interfaces::display::pixel_format_bytes(asset.format);
    }

    std::uint32_t stripe_height(std::uint32_t y, std::uint32_t rows, std::uint32_t total)
    {
        return total - y < rows ? total - y : rows;
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
    const auto row_bytes = row_bytes_of(asset);
    if (!asset.valid() || scratch == nullptr || destination.empty()
        || destination.width != static_cast<std::int32_t>(asset.width)
        || destination.height != static_cast<std::int32_t>(asset.height) || asset.stride_bytes < row_bytes
        || row_bytes == 0)
        return Status::invalid_argument;
    if (scratch_bytes < row_bytes)
        return Status::no_memory;
    if (reader_ == nullptr)
        return Status::not_ready;
    if (display_.capabilities().async_transfer && scratch_bytes >= 2 * row_bytes)
        return stream_pipelined(asset, destination, scratch, scratch_bytes);

    const auto rows = static_cast<std::uint32_t>(scratch_bytes / row_bytes);
    for (std::uint32_t y = 0; y < asset.height; y += rows)
    {
        const auto height = stripe_height(y, rows, asset.height);
        if (const Status status = read_stripe(asset, y, height, scratch, row_bytes); !is_ok(status))
            return status;
        if (const Status status = submit_stripe(asset, destination, y, height, scratch); !is_ok(status))
            return status;
        if (const Status status = display_.wait_for_transfer_complete(config_.transfer_timeout_ms); !is_ok(status))
            return status;
    }
    return Status::ok;
}

Status AssetStreamer::stream_pipelined(const brick::interfaces::display::ImageAsset& asset,
                                       brick::interfaces::display::DisplayRect destination, std::uint8_t* scratch,
                                       std::size_t scratch_bytes) const
{
    const auto          row_bytes  = row_bytes_of(asset);
    const std::size_t   half_bytes = scratch_bytes / 2;
    const auto          rows       = static_cast<std::uint32_t>(half_bytes / row_bytes);
    std::uint8_t* const halves[2]  = { scratch, scratch + half_bytes };
    std::size_t         next       = 0;
    bool                pending    = false;
    for (std::uint32_t y = 0; y < asset.height; y += rows)
    {
        // The half being read is free: its last transfer was waited for
        // before the other half was submitted.
        const auto height = stripe_height(y, rows, asset.height);
        Status     status = read_stripe(asset, y, height, halves[next], row_bytes);
        if (pending)
        {
            // Also after a failed read: scratch must not be in use on return.
            const Status waited = display_.wait_for_transfer_complete(config_.transfer_timeout_ms);
            pending             = false;
            if (is_ok(status))
                status = waited;
        }
        if (is_ok(status))
            status = submit_stripe(asset, destination, y, height, halves[next]);
        if (!is_ok(status))
            return status;
        pending = true;
        next ^= 1U;
    }
    return pending ? display_.wait_for_transfer_complete(config_.transfer_timeout_ms) : Status::ok;
}

Status AssetStreamer::stream_to_buffer(const brick::interfaces::display::ImageAsset&   asset,
                                       brick::interfaces::display::WritablePixelBuffer destination,
                                       std::uint8_t* scratch, std::size_t scratch_bytes) const
{
    const auto row_bytes = row_bytes_of(asset);
    if (!asset.valid() || !destination.valid() || scratch == nullptr || destination.width != asset.width
        || destination.height != asset.height || destination.format != asset.format
        || destination.stride_bytes < row_bytes || row_bytes == 0 || asset.stride_bytes < row_bytes)
        return Status::invalid_argument;
    if (scratch_bytes < row_bytes)
        return Status::no_memory;
    if (reader_ == nullptr)
        return Status::not_ready;

    const auto rows = static_cast<std::uint32_t>(scratch_bytes / row_bytes);
    // Packed destination rows: the reader writes into the destination itself,
    // which saves copying the whole image once more.
    const bool direct = destination.stride_bytes == row_bytes;
    for (std::uint32_t y = 0; y < asset.height; y += rows)
    {
        const auto    height   = stripe_height(y, rows, asset.height);
        std::uint8_t* rows_out = destination.data + static_cast<std::size_t>(y) * destination.stride_bytes;
        if (direct)
        {
            if (const Status status = read_stripe(asset, y, height, rows_out, row_bytes); !is_ok(status))
                return status;
            continue;
        }
        if (const Status status = read_stripe(asset, y, height, scratch, row_bytes); !is_ok(status))
            return status;
        for (std::uint32_t row = 0; row < height; ++row)
            std::memcpy(rows_out + static_cast<std::size_t>(row) * destination.stride_bytes,
                        scratch + static_cast<std::size_t>(row) * row_bytes, row_bytes);
    }
    return Status::ok;
}

Status AssetStreamer::read_stripe(const brick::interfaces::display::ImageAsset& asset, std::uint32_t y,
                                  std::uint32_t height, std::uint8_t* destination, std::size_t destination_stride) const
{
    const auto        row_bytes = row_bytes_of(asset);
    const std::size_t offset    = static_cast<std::size_t>(y) * asset.stride_bytes;
    // The last row ends row_bytes after its start; the stride padding after
    // it does not have to be in the data.
    const std::size_t end = offset + static_cast<std::size_t>(height - 1U) * asset.stride_bytes + row_bytes;
    if (end > asset.data_size)
        return Status::error;
    if (asset.stride_bytes == row_bytes && destination_stride == row_bytes)
        return reader_->read(asset, offset, destination, static_cast<std::size_t>(height) * row_bytes);
    for (std::uint32_t row = 0; row < height; ++row)
    {
        if (const Status status =
                reader_->read(asset, offset + static_cast<std::size_t>(row) * asset.stride_bytes,
                              destination + static_cast<std::size_t>(row) * destination_stride, row_bytes);
            !is_ok(status))
            return status;
    }
    return Status::ok;
}

Status AssetStreamer::submit_stripe(const brick::interfaces::display::ImageAsset& asset,
                                    brick::interfaces::display::DisplayRect destination, std::uint32_t y,
                                    std::uint32_t height, const std::uint8_t* pixels) const
{
    const auto                                    row_bytes = row_bytes_of(asset);
    const brick::interfaces::display::PixelBuffer buffer{ pixels, asset.width, height, row_bytes, asset.format, false };
    const brick::interfaces::display::DisplayRect stripe{ destination.x, destination.y + static_cast<std::int32_t>(y),
                                                          destination.width, static_cast<std::int32_t>(height) };
    return display_.submit_buffer(stripe, buffer);
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
