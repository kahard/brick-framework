#pragma once

#include <cstdint>

namespace brick::interfaces::audio {

class IAudioOutput {
 public:
  virtual ~IAudioOutput() = default;
  virtual bool begin() = 0;
  virtual void write_sample(std::uint8_t sample) = 0;
  virtual void stop() = 0;
};

}  // namespace brick::interfaces::audio
