#pragma once

#include <cstddef>
#include <cstdint>

namespace brick::interfaces::display {

enum class TouchState : std::uint8_t {
  released,
  pressed,
  moved,
};

struct TouchPoint {
  std::uint8_t id = 0;
  std::int16_t x = 0;
  std::int16_t y = 0;
  std::int16_t raw_x = 0;
  std::int16_t raw_y = 0;
  std::int16_t pressure = 0;
  TouchState state = TouchState::released;
};

class ITouchscreen {
 public:
  virtual ~ITouchscreen() = default;

  virtual bool begin() = 0;

  // Returns true when a new sample is available. The implementation writes at
  // most capacity points and returns the number of valid points in count.
  virtual bool read(TouchPoint* points, std::size_t capacity,
                    std::size_t& count) = 0;
};

}  // namespace brick::interfaces::display
