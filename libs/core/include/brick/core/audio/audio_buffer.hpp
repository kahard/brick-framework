#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace brick::core::audio {

class AudioBuffer {
 public:
  AudioBuffer() = default;
  AudioBuffer(std::vector<std::uint8_t> samples, std::uint32_t sample_rate)
      : samples_(std::move(samples)), sample_rate_(sample_rate) {}

  const std::uint8_t* data() const { return samples_.data(); }
  std::size_t size() const { return samples_.size(); }
  std::uint32_t sample_rate() const { return sample_rate_; }
  bool empty() const { return samples_.empty(); }

  void clear() {
    samples_.clear();
    sample_rate_ = 0;
  }

  void assign(std::vector<std::uint8_t> samples, std::uint32_t sample_rate) {
    samples_ = std::move(samples);
    sample_rate_ = sample_rate;
  }

 private:
  std::vector<std::uint8_t> samples_;
  std::uint32_t sample_rate_ = 0;
};

}  // namespace brick::core::audio
