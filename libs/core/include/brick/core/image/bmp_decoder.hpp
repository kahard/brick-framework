#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "brick/interfaces/storage/file_system.hpp"

namespace brick::core::image {

struct BmpDecodeOptions {
  std::uint32_t target_width;
  std::uint32_t target_height;
  void (*on_row)(std::uint32_t row) = nullptr;
};

class BmpDecoder {
 public:
  static bool decode(interfaces::storage::IFileSystem& file_system,
                     const char* path, std::uint8_t* destination,
                     const BmpDecodeOptions& options) {
    auto file = file_system.open(path, "rb");
    if (!file || destination == nullptr || options.target_width == 0 ||
        options.target_height == 0) {
      return false;
    }

    std::uint8_t header[54];
    if (file->read(header, 1, sizeof(header)) < sizeof(header) ||
        header[0] != 'B' || header[1] != 'M') {
      return false;
    }

    const std::uint32_t pixel_offset = read_u32(header + 10);
    const std::int32_t source_width = read_i32(header + 18);
    std::int32_t source_height = read_i32(header + 22);
    const std::uint16_t bits_per_pixel = read_u16(header + 28);
    if (source_width <= 0 || source_height == 0 ||
        (bits_per_pixel != 24 && bits_per_pixel != 32)) {
      return false;
    }

    const bool bottom_up = source_height > 0;
    if (source_height < 0) {
      source_height = -source_height;
    }
    const int bytes_per_pixel = bits_per_pixel / 8;
    const int stride = ((source_width * bytes_per_pixel + 3) / 4) * 4;
    const int copy_width = source_width < static_cast<std::int32_t>(options.target_width)
                               ? source_width : static_cast<int>(options.target_width);
    const int copy_height = source_height < static_cast<std::int32_t>(options.target_height)
                                ? source_height : static_cast<int>(options.target_height);
    const int x_offset = (static_cast<int>(options.target_width) - copy_width) / 2;
    const int y_offset = (static_cast<int>(options.target_height) - copy_height) / 2;
    const long skip_rows = source_height > static_cast<std::int32_t>(options.target_height)
                               ? (source_height - static_cast<std::int32_t>(options.target_height)) / 2
                               : 0;

    std::fill_n(destination, static_cast<std::size_t>(options.target_width) *
                                      options.target_height * 2, 0);
    auto* row = static_cast<std::uint8_t*>(std::malloc(stride));
    if (row == nullptr || !file->seek(static_cast<long>(pixel_offset) + skip_rows * stride, SEEK_SET)) {
      std::free(row);
      return false;
    }

    for (int source_row = 0; source_row < copy_height; ++source_row) {
      if (file->read(row, 1, stride) != static_cast<std::size_t>(stride)) {
        std::free(row);
        return false;
      }
      if (options.on_row != nullptr && (source_row & 31) == 0) {
        options.on_row(static_cast<std::uint32_t>(source_row));
      }
      const int destination_row = bottom_up
                                      ? copy_height - 1 - source_row + y_offset
                                      : source_row + y_offset;
      for (int column = 0; column < copy_width; ++column) {
        const auto* pixel = row + column * bytes_per_pixel;
        const std::uint16_t rgb565 = static_cast<std::uint16_t>(
            ((pixel[2] & 0xF8) << 8) | ((pixel[1] & 0xFC) << 3) | (pixel[0] >> 3));
        const std::size_t index =
            (static_cast<std::size_t>(destination_row) * options.target_width +
             column + x_offset) * 2;
        // LVGL_COLOR_FORMAT_RGB565 stores the 16-bit pixel in native
        // little-endian byte order. The display driver's byte-order setting
        // is applied while flushing to the panel, not to image descriptors.
        destination[index] = static_cast<std::uint8_t>(rgb565 & 0xFF);
        destination[index + 1] = static_cast<std::uint8_t>(rgb565 >> 8);
      }
    }

    std::free(row);
    return true;
  }

 private:
  static std::uint16_t read_u16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0] | (value[1] << 8));
  }

  static std::uint32_t read_u32(const std::uint8_t* value) {
    return static_cast<std::uint32_t>(value[0] | (value[1] << 8) |
                                      (value[2] << 16) | (value[3] << 24));
  }

  static std::int32_t read_i32(const std::uint8_t* value) {
    return static_cast<std::int32_t>(read_u32(value));
  }
};

}  // namespace brick::core::image
