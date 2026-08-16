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

  const std::uint8_t* data() const {
    return view_data_ != nullptr ? view_data_ : samples_.data();
  }
  std::size_t size() const {
    return view_data_ != nullptr ? view_size_ : samples_.size();
  }
  std::uint32_t sample_rate() const { return sample_rate_; }
  bool empty() const { return size() == 0; }

  void clear() {
    view_data_ = nullptr;
    view_size_ = 0;
    samples_.clear();
    sample_rate_ = 0;
  }

  void assign(std::vector<std::uint8_t> samples, std::uint32_t sample_rate) {
    view_data_ = nullptr;
    view_size_ = 0;
    samples_ = std::move(samples);
    sample_rate_ = sample_rate;
  }

  // Reference an externally owned sample buffer without copying it. The
  // caller must keep the memory valid until clear() or another assign().
  void assign_view(const std::uint8_t* data, std::size_t size,
                   std::uint32_t sample_rate) {
    samples_.clear();
    view_data_ = data;
    view_size_ = size;
    sample_rate_ = sample_rate;
  }

 private:
  std::vector<std::uint8_t> samples_;
  const std::uint8_t* view_data_ = nullptr;
  std::size_t view_size_ = 0;
  std::uint32_t sample_rate_ = 0;
};

}  // namespace brick::core::audio
