#pragma once

#include "brick/core/audio/AudioBuffer.h"

namespace brick::interfaces::audio
{

class IAudioPlayer
{
public:
    virtual ~IAudioPlayer()                                   = default;
    virtual bool begin()                                      = 0;
    virtual bool play(const core::audio::AudioBuffer& buffer) = 0;
    virtual void tick()                                       = 0;
    virtual void stop()                                       = 0;
    virtual bool playing() const                              = 0;
};

}  // namespace brick::interfaces::audio
