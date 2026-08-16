#pragma once

#include <algorithm>
#include <cstdint>

#include "brick/interfaces/display/display_types.hpp"
#include "brick/interfaces/display/touchscreen.hpp"

namespace brick::core::input {

struct TouchCalibration {
  std::int16_t raw_x_min = 0;
  std::int16_t raw_x_max = 0;
  std::int16_t raw_y_min = 0;
  std::int16_t raw_y_max = 0;
  bool invert_x = false;
  bool invert_y = false;
  bool swap_xy = false;
};

class TouchMapper {
 public:
  TouchMapper(brick::interfaces::display::DisplaySize display_size,
              TouchCalibration calibration)
      : display_size_(display_size), calibration_(calibration) {}

  brick::interfaces::display::TouchPoint map(
      std::uint8_t id, std::int16_t raw_x, std::int16_t raw_y,
      std::int16_t pressure = 0) const {
    auto x = raw_x;
    auto y = raw_y;
    auto x_min = calibration_.raw_x_min;
    auto x_max = calibration_.raw_x_max;
    auto y_min = calibration_.raw_y_min;
    auto y_max = calibration_.raw_y_max;

    if (calibration_.swap_xy) {
      std::swap(x, y);
      std::swap(x_min, y_min);
      std::swap(x_max, y_max);
    }

    auto mapped_x = normalize(x, x_min, x_max,
                              calibration_.invert_x,
                              display_size_.width);
    auto mapped_y = normalize(y, y_min, y_max,
                              calibration_.invert_y,
                              display_size_.height);

    brick::interfaces::display::TouchPoint point;
    point.id = id;
    point.raw_x = raw_x;
    point.raw_y = raw_y;
    point.x = mapped_x;
    point.y = mapped_y;
    point.pressure = pressure;
    point.state = brick::interfaces::display::TouchState::moved;
    return point;
  }

 private:
  static std::int16_t normalize(std::int16_t value, std::int16_t minimum,
                                std::int16_t maximum, bool inverted,
                                std::uint16_t output_size) {
    if (output_size == 0 || maximum <= minimum) return 0;
    const auto clamped = std::clamp(value, minimum, maximum);
    const auto numerator = static_cast<std::int32_t>(clamped - minimum) *
                           static_cast<std::int32_t>(output_size - 1);
    auto result = static_cast<std::int16_t>(numerator / (maximum - minimum));
    if (inverted) result = static_cast<std::int16_t>(output_size - 1 - result);
    return result;
  }

  brick::interfaces::display::DisplaySize display_size_;
  TouchCalibration calibration_;
};

}  // namespace brick::core::input
