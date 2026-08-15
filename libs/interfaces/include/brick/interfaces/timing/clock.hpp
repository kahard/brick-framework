#pragma once

#include <cstdint>

namespace brick::interfaces::timing {

class IClock {
 public:
  virtual ~IClock() = default;
  virtual std::uint32_t millis() const = 0;
};

}  // namespace brick::interfaces::timing
