#pragma once

#include <cstdint>

#include "brick/interfaces/audio/IAudioOutput.h"
#include "driver/ledc.h"

namespace brick::platform::esp32
{

class PwmAudioOutput final : public interfaces::audio::IAudioOutput
{
public:
    PwmAudioOutput(int pin, int channel, int carrier_hz);

    bool begin() override;

    void write_sample(std::uint8_t sample) override;

    void stop() override;

private:
    static constexpr ledc_mode_t mode_ = LEDC_LOW_SPEED_MODE;
    int                          pin_;
    ledc_channel_t               channel_;
    int                          carrier_hz_;
    bool                         initialized_ = false;
};

}  // namespace brick::platform::esp32
