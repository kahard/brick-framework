#pragma once

#include <cstddef>
#include <cstdint>

namespace brick::interfaces::display {

struct DisplaySize {
  std::uint16_t width = 0;
  std::uint16_t height = 0;
};

enum class PixelFormat : std::uint8_t {
  rgb565,
  rgb888,
};

enum class Rotation : std::uint8_t {
  rotate_0,
  rotate_90,
  rotate_180,
  rotate_270,
};

}  // namespace brick::interfaces::display
