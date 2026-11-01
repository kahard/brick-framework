#include "brick/core/image/BmpDecoder.h"

#include <algorithm>
#include <cstdio>

namespace brick::core::image
{

namespace
{

    constexpr std::size_t kHeaderBytes = 54;
    constexpr int         kRgb565Bytes = 2;
    // Keeps every offset computed from the header within 32 bits.
    constexpr std::int32_t  kMaxDimension        = 16384;
    constexpr std::uint32_t kMaxPixelOffset      = 1U << 24;
    constexpr int           kRowProgressInterval = 32;

}  // namespace

Status BmpDecoder::decode(interfaces::storage::IFileSystem& file_system, const char* path, std::uint8_t* destination,
                          const BmpDecodeOptions& options)
{
    constexpr auto kMaxTarget = static_cast<std::uint32_t>(kMaxDimension);
    if (destination == nullptr || options.target_width == 0 || options.target_height == 0
        || options.target_width > kMaxTarget || options.target_height > kMaxTarget)
        return Status::invalid_argument;
    interfaces::storage::FileHandle file;
    if (const Status status = file_system.open(path, "rb", file); !is_ok(status))
        return status;
    Layout layout;
    if (const Status status = read_layout(*file, options, layout); !is_ok(status))
        return status;

    std::fill_n(destination, static_cast<std::size_t>(options.target_width) * options.target_height * kRgb565Bytes, 0);
    for (int source_row = 0; source_row < layout.copy_height; ++source_row)
    {
        const long row_offset =
            static_cast<long>(layout.pixel_offset) + static_cast<long>(layout.skip_rows + source_row) * layout.stride;
        if (const Status status = file->seek(row_offset, SEEK_SET); !is_ok(status))
            return status;
        if (options.on_row != nullptr && source_row % kRowProgressInterval == 0)
            options.on_row(static_cast<std::uint32_t>(source_row));
        const int destination_row =
            layout.y_offset + (layout.bottom_up ? layout.copy_height - 1 - source_row : source_row);
        std::uint8_t* row =
            destination
            + (static_cast<std::size_t>(destination_row) * options.target_width + layout.x_offset) * kRgb565Bytes;
        if (const Status status = copy_row(*file, layout, row); !is_ok(status))
            return status;
    }
    return Status::ok;
}

Status BmpDecoder::read_layout(interfaces::storage::IFile& file, const BmpDecodeOptions& options, Layout& layout)
{
    std::uint8_t header[kHeaderBytes];
    if (file.read(header, 1, sizeof(header)) < sizeof(header) || header[0] != 'B' || header[1] != 'M')
        return Status::error;

    const std::int32_t  source_width   = read_i32(header + 18);
    const std::int32_t  height_field   = read_i32(header + 22);
    const std::uint16_t bits_per_pixel = read_u16(header + 28);
    layout.pixel_offset                = read_u32(header + 10);
    if (source_width <= 0 || height_field == 0 || layout.pixel_offset > kMaxPixelOffset)
        return Status::error;
    if ((bits_per_pixel != 24 && bits_per_pixel != 32) || source_width > kMaxDimension || height_field > kMaxDimension
        || height_field < -kMaxDimension)
        return Status::not_supported;

    // A positive height means the rows are stored bottom-up.
    const std::int32_t source_height = height_field > 0 ? height_field : -height_field;
    const auto         target_width  = static_cast<std::int32_t>(options.target_width);
    const auto         target_height = static_cast<std::int32_t>(options.target_height);
    layout.bottom_up                 = height_field > 0;
    layout.bytes_per_pixel           = bits_per_pixel / 8;
    layout.stride                    = ((source_width * layout.bytes_per_pixel + 3) / 4) * 4;
    layout.copy_width                = std::min(source_width, target_width);
    layout.copy_height               = std::min(source_height, target_height);
    layout.x_offset                  = (target_width - layout.copy_width) / 2;
    layout.y_offset                  = (target_height - layout.copy_height) / 2;
    layout.skip_rows                 = (source_height - layout.copy_height) / 2;
    return Status::ok;
}

Status BmpDecoder::copy_row(interfaces::storage::IFile& file, const Layout& layout, std::uint8_t* row)
{
    // The copied part of a row is read through a small stack buffer, so no
    // row-sized allocation is needed. 96 bytes hold whole 24- and 32-bit pixels.
    std::uint8_t chunk[96];
    const int    pixels_per_chunk = static_cast<int>(sizeof(chunk)) / layout.bytes_per_pixel;
    for (int column = 0; column < layout.copy_width;)
    {
        const int         count = std::min(pixels_per_chunk, layout.copy_width - column);
        const std::size_t bytes = static_cast<std::size_t>(count) * layout.bytes_per_pixel;
        if (file.read(chunk, 1, bytes) != bytes)
            return Status::error;
        for (int index = 0; index < count; ++index, ++column)
        {
            // BMP stores blue, green, red.
            const std::uint8_t* pixel = chunk + index * layout.bytes_per_pixel;
            const std::uint16_t rgb565 =
                static_cast<std::uint16_t>(((pixel[2] & 0xF8) << 8) | ((pixel[1] & 0xFC) << 3) | (pixel[0] >> 3));
            row[column * kRgb565Bytes]     = static_cast<std::uint8_t>(rgb565 & 0xFF);
            row[column * kRgb565Bytes + 1] = static_cast<std::uint8_t>(rgb565 >> 8);
        }
    }
    return Status::ok;
}

std::uint16_t BmpDecoder::read_u16(const std::uint8_t* value)
{
    return static_cast<std::uint16_t>(value[0] | (value[1] << 8));
}

std::uint32_t BmpDecoder::read_u32(const std::uint8_t* value)
{
    return static_cast<std::uint32_t>(value[0] | (value[1] << 8) | (value[2] << 16) | (value[3] << 24));
}

std::int32_t BmpDecoder::read_i32(const std::uint8_t* value)
{
    return static_cast<std::int32_t>(read_u32(value));
}

}  // namespace brick::core::image
