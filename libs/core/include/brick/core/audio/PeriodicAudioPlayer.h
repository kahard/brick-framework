#pragma once

#include <cstddef>

#include "brick/core/audio/AudioBuffer.h"
#include "brick/interfaces/audio/IAudioOutput.h"
#include "brick/interfaces/audio/IAudioPlayer.h"

namespace brick::core::audio
{

class PeriodicAudioPlayer final : public interfaces::audio::IAudioPlayer
{
public:
    explicit PeriodicAudioPlayer(interfaces::audio::IAudioOutput& output);

    bool begin() override;
    bool play(const AudioBuffer& buffer) override;
    void tick() override;
    void stop() override;
    bool playing() const override;

private:
    interfaces::audio::IAudioOutput& output_;
    const AudioBuffer*               current_ = nullptr;
    std::size_t                      index_   = 0;
    bool                             playing_ = false;
};

}  // namespace brick::core::audio
