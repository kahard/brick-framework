#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/audio/IPcmOutput.h"
#include "brick/interfaces/storage/IByteStorage.h"

namespace brick::core::audio
{

/// Where a PcmPlayer takes its frames from.
class IPcmSource
{
public:
    virtual ~IPcmSource() = default;

    virtual interfaces::audio::PcmFormat format() const = 0;
    /// Copies up to capacity frames (format().channels samples each); count
    /// is the number copied. Status::no_data with count 0 at the end.
    virtual Status read(std::int16_t* frames, std::size_t capacity, std::size_t& count) = 0;
    /// Back to the first frame.
    virtual Status rewind() = 0;
};

enum class Waveform : std::uint8_t
{
    sine,
    square,
};

/// A tone of one frequency (mono), with 5 ms fades so it starts and ends
/// without a click.
class ToneSource final : public IPcmSource
{
public:
    /// amplitude: 0..100 % of full scale.
    ToneSource(std::uint32_t frequency_hz, std::uint32_t duration_ms, std::uint32_t sample_rate = 22050,
               Waveform waveform = Waveform::sine, std::uint8_t amplitude = 50);

    interfaces::audio::PcmFormat format() const override { return { sample_rate_, 1 }; }
    Status                       read(std::int16_t* frames, std::size_t capacity, std::size_t& count) override;
    Status                       rewind() override;

private:
    static constexpr std::size_t kTableSize = 256;

    std::int16_t sample_(std::uint32_t phase) const;
    std::int32_t envelope_(std::uint32_t frame) const;

    std::array<std::int16_t, kTableSize> sine_{};
    std::uint32_t                        sample_rate_;
    std::uint32_t                        phase_step_;
    std::uint32_t                        total_frames_;
    std::uint32_t                        fade_frames_;
    std::uint32_t                        frame_ = 0;
    std::uint32_t                        phase_ = 0;
    std::int32_t                         peak_;
    Waveform                             waveform_;
};

/// Read-only bytes in memory (e.g. a WAV file compiled into the firmware or
/// memory-mapped flash) as IByteStorage, for WavSource.
class MemoryBytes final : public interfaces::storage::IByteStorage
{
public:
    MemoryBytes(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    Status read(std::uint32_t address, void* data, std::size_t size) override;
    Status write(std::uint32_t, const void*, std::size_t) override { return Status::not_supported; }

private:
    const std::uint8_t* data_;
    std::size_t         size_;
};

/// A PCM WAV file (8- or 16-bit, mono or stereo) read in pieces from byte
/// storage at address: nothing but the header is kept in RAM. 8-bit samples
/// become 16-bit.
class WavSource final : public IPcmSource
{
public:
    WavSource(interfaces::storage::IByteStorage& storage, std::uint32_t address = 0, std::uint32_t size = 0);

    /// Reads the header. Status::not_supported for a WAV other than 8/16-bit
    /// PCM with one or two channels, Status::error for something else.
    Status                       open();
    interfaces::audio::PcmFormat format() const override { return format_; }
    Status                       read(std::int16_t* frames, std::size_t capacity, std::size_t& count) override;
    Status                       rewind() override;
    /// Length in milliseconds (after open()).
    std::uint32_t duration_ms() const;

private:
    interfaces::storage::IByteStorage& storage_;
    std::uint32_t                      address_;
    std::uint32_t                      size_;
    interfaces::audio::PcmFormat       format_{ 0, 1 };
    std::uint8_t                       bytes_per_sample_ = 2;
    std::uint32_t                      data_offset_      = 0;
    std::uint32_t                      data_size_        = 0;
    std::uint32_t                      position_         = 0;  ///< bytes of data read
};

/// Plays an IPcmSource on an IPcmOutput: update() moves frames into the
/// output's buffers whenever they have room, never waiting. The output keeps
/// running (silent) after the end; stop() it to switch the amplifier off.
class PcmPlayer final
{
public:
    static constexpr std::size_t kChunkFrames = 256;

    explicit PcmPlayer(interfaces::audio::IPcmOutput& output) : output_(output) {}

    /// Plays source from its current position, (re)starting the output in the
    /// source's format when it differs. The source must stay valid while it
    /// plays.
    Status play(IPcmSource& source);
    /// Stops feeding frames (what is queued still plays out).
    void stop();
    void update();
    bool playing() const { return source_ != nullptr; }
    /// Frames handed to the output since construction (they play at the
    /// output's rate, so this over time measures it).
    std::uint64_t frames_sent() const { return frames_sent_; }

private:
    interfaces::audio::IPcmOutput&             output_;
    IPcmSource*                                source_ = nullptr;
    std::array<std::int16_t, kChunkFrames * 2> chunk_{};
    std::size_t                                chunk_frames_ = 0;
    std::size_t                                sent_frames_  = 0;
    std::uint8_t                               channels_     = 1;
    std::uint64_t                              frames_sent_  = 0;
};

}  // namespace brick::core::audio
