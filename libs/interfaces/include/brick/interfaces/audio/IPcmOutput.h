#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"

namespace brick::interfaces::audio
{

/// 16-bit signed PCM: sample_rate frames per second, channels samples per
/// frame (interleaved, left first).
struct PcmFormat
{
    std::uint32_t sample_rate = 22050;
    std::uint8_t  channels    = 1;
};

constexpr bool operator==(const PcmFormat& left, const PcmFormat& right)
{
    return left.sample_rate == right.sample_rate && left.channels == right.channels;
}

constexpr bool operator!=(const PcmFormat& left, const PcmFormat& right)
{
    return !(left == right);
}

/// Streaming audio output (I2S with a codec or an amplifier, a DAC with DMA,
/// the PC's sound card): frames queue into the driver's DMA buffers without
/// waiting, so a loop keeps them filled from update().
class IPcmOutput
{
public:
    virtual ~IPcmOutput() = default;

    /// Starts the output for format (clocks, codec, amplifier); it plays
    /// silence until frames come. A running output switches to the new
    /// format. Status::not_supported for a rate or channel count the hardware
    /// cannot play.
    virtual Status start(const PcmFormat& format) = 0;
    /// Queues up to count frames without waiting; written is the number taken
    /// (Status::busy with written 0 when the buffers are full). Status::not_ready
    /// when not started.
    virtual Status write(const std::int16_t* frames, std::size_t count, std::size_t& written) = 0;
    /// Loudness 0..100 (codec gain, or scaling the samples without a codec).
    virtual Status       set_volume(std::uint8_t percent) = 0;
    virtual std::uint8_t volume() const                   = 0;
    /// Silences the output and switches the amplifier off; start() plays again.
    virtual void stop() = 0;
    /// The format of the running output; sample_rate 0 when stopped.
    virtual PcmFormat format() const = 0;
};

}  // namespace brick::interfaces::audio
