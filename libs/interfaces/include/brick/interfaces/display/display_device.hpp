#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/display_types.hpp"

namespace brick::interfaces::display {

class IDisplayDevice {
 public:
  virtual ~IDisplayDevice() = default;

  virtual bool begin() = 0;
  virtual DisplaySize size() const = 0;
  virtual PixelFormat pixel_format() const = 0;
  virtual bool set_rotation(Rotation rotation) = 0;

  // Coordinates use the currently selected rotation and are inclusive at the
  // start, exclusive at the end, matching common embedded display APIs.
  virtual bool draw_pixels(std::uint16_t x, std::uint16_t y,
                           std::uint16_t width, std::uint16_t height,
                           const std::uint8_t* pixels,
                           std::size_t byte_count) = 0;
};

}  // namespace brick::interfaces::display
