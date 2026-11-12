#pragma once

#include <utility>

#include "brick/Status.h"
#include "brick/platform/esp32/audio/Es8311Codec.h"
#include "brick/platform/esp32/audio/I2sPcmOutput.h"

namespace brick::platform::esp32::audio
{

/// The audio path of a board with an ES8311: its I2C registers (Registers:
/// LegacyI2cRegisters or MasterI2cRegisters, after the touch screen's I2C
/// driver), the codec and the I2S output that drives it.
template <typename Registers>
class Es8311I2sAudio final
{
public:
    template <typename... RegisterArgs>
    explicit Es8311I2sAudio(const I2sPcmOutputConfig& config, RegisterArgs&&... register_args)
        : registers_(std::forward<RegisterArgs>(register_args)...), codec_(registers_), output_(config, &codec_)
    {
    }
    Es8311I2sAudio(const Es8311I2sAudio&)            = delete;
    Es8311I2sAudio& operator=(const Es8311I2sAudio&) = delete;

    /// Nothing to start: output().start() sets the codec up when it plays.
    Status begin() { return Status::ok; }

    I2sPcmOutput& output() { return output_; }
    Registers&    registers() { return registers_; }

private:
    Registers    registers_;
    Es8311Codec  codec_;
    I2sPcmOutput output_;
};

}  // namespace brick::platform::esp32::audio
