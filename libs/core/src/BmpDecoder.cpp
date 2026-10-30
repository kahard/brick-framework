#include "brick/core/image/BmpDecoder.h"

#include <algorithm>
#include <cstdio>

namespace brick::core::image
{

bool BmpDecoder::decode(interfaces::storage::IFileSystem& file_system, const char* path, std::uint8_t* destination,
                        const BmpDecodeOptions& options)
{
    interfaces::storage::FileHandle file;
    if (!file_system.open(path, "rb", file) || destination == nullptr || options.target_width == 0
        || options.target_height == 0)
        return false;

    std::uint8_t header[54];
    if (file->read(header, 1, sizeof(header)) < sizeof(header) || header[0] != 'B' || header[1] != 'M')
        return false;

    const std::uint32_t pixel_offset   = read_u32(header + 10);
    const std::int32_t  source_width   = read_i32(header + 18);
    std::int32_t        source_height  = read_i32(header + 22);
    const std::uint16_t bits_per_pixel = read_u16(header + 28);
    if (source_width <= 0 || source_height == 0 || (bits_per_pixel != 24 && bits_per_pixel != 32))
        return false;

    const bool bottom_up = source_height > 0;
    if (source_height < 0)
        source_height = -source_height;
    const int  bytes_per_pixel = bits_per_pixel / 8;
    const int  stride          = ((source_width * bytes_per_pixel + 3) / 4) * 4;
    const int  copy_width      = std::min(source_width, static_cast<std::int32_t>(options.target_width));
    const int  copy_height     = std::min(source_height, static_cast<std::int32_t>(options.target_height));
    const int  x_offset        = (static_cast<int>(options.target_width) - copy_width) / 2;
    const int  y_offset        = (static_cast<int>(options.target_height) - copy_height) / 2;
    const long skip_rows       = source_height > static_cast<std::int32_t>(options.target_height) ?
                                     (source_height - static_cast<std::int32_t>(options.target_height)) / 2 :
                                     0;

    std::fill_n(destination, static_cast<std::size_t>(options.target_width) * options.target_height * 2, 0);
    // The copied part of each row is read through a small stack buffer, so no
    // row-sized allocation is needed. 96 bytes hold whole 24- and 32-bit pixels.
    std::uint8_t chunk[96];
    const int    pixels_per_chunk = static_cast<int>(sizeof(chunk)) / bytes_per_pixel;
    for (int source_row = 0; source_row < copy_height; ++source_row)
    {
        const long row_offset = static_cast<long>(pixel_offset) + (skip_rows + source_row) * stride;
        if (!file->seek(row_offset, SEEK_SET))
            return false;
        if (options.on_row != nullptr && (source_row & 31) == 0)
            options.on_row(static_cast<std::uint32_t>(source_row));
        const int destination_row = bottom_up ? copy_height - 1 - source_row + y_offset : source_row + y_offset;
        for (int column = 0; column < copy_width;)
        {
            const int         count = std::min(pixels_per_chunk, copy_width - column);
            const std::size_t bytes = static_cast<std::size_t>(count) * bytes_per_pixel;
            if (file->read(chunk, 1, bytes) != bytes)
                return false;
            for (int index = 0; index < count; ++index, ++column)
            {
                const std::uint8_t* pixel = chunk + index * bytes_per_pixel;
                const std::uint16_t rgb565 =
                    static_cast<std::uint16_t>(((pixel[2] & 0xF8) << 8) | ((pixel[1] & 0xFC) << 3) | (pixel[0] >> 3));
                const std::size_t offset =
                    (static_cast<std::size_t>(destination_row) * options.target_width + column + x_offset) * 2;
                destination[offset]     = static_cast<std::uint8_t>(rgb565 & 0xFF);
                destination[offset + 1] = static_cast<std::uint8_t>(rgb565 >> 8);
            }
        }
    }
    return true;
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
