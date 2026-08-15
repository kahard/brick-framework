#pragma once

#include "brick/core/audio/audio_buffer.hpp"
#include "brick/interfaces/audio/audio_output.hpp"
#include "brick/interfaces/audio/audio_player.hpp"

namespace brick::core::audio {

class PeriodicAudioPlayer final : public interfaces::audio::IAudioPlayer {
 public:
  explicit PeriodicAudioPlayer(interfaces::audio::IAudioOutput& output)
      : output_(output) {}

  bool begin() override { return output_.begin(); }

  bool play(const AudioBuffer& buffer) override {
    if (buffer.empty() || !output_.begin()) {
      return false;
    }
    current_ = &buffer;
    index_ = 0;
    playing_ = true;
    return true;
  }

  void tick() override {
    if (!playing_ || current_ == nullptr) {
      return;
    }
    output_.write_sample(current_->data()[index_++]);
    if (index_ >= current_->size()) {
      stop();
    }
  }

  void stop() override {
    if (!playing_) {
      return;
    }
    playing_ = false;
    current_ = nullptr;
    index_ = 0;
    output_.write_sample(128);
    output_.stop();
  }

  bool playing() const override { return playing_; }

 private:
  interfaces::audio::IAudioOutput& output_;
  const AudioBuffer* current_ = nullptr;
  std::size_t index_ = 0;
  bool playing_ = false;
};

}  // namespace brick::core::audio
