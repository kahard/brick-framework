#include "brick/core/audio/PcmSource.h"

#include <cmath>
#include <cstring>

namespace brick::core::audio
{

namespace
{
    constexpr std::uint32_t kFadeMs      = 5;
    constexpr std::int32_t  kFullScale   = 32767;
    constexpr std::uint32_t kPhaseBits   = 24;  ///< the table index takes the top 8 bits of the 32-bit phase
    constexpr float         kTwoPi       = 6.2831853F;
    constexpr std::uint64_t kPhaseRange  = 1ULL << 32;
    constexpr std::uint32_t kRiffHeader  = 12;
    constexpr std::uint32_t kChunkHeader = 8;
    constexpr std::uint16_t kPcmEncoding = 1;

    std::uint16_t le16(const std::uint8_t* data)
    {
        return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
    }

    std::uint32_t le32(const std::uint8_t* data)
    {
        return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8)
               | (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
    }
}  // namespace

// ---- ToneSource ---------------------------------------------------------------

ToneSource::ToneSource(std::uint32_t frequency_hz, std::uint32_t duration_ms, std::uint32_t sample_rate,
                       Waveform waveform, std::uint8_t amplitude)
    : sample_rate_(sample_rate == 0 ? 1 : sample_rate),
      phase_step_(static_cast<std::uint32_t>(kPhaseRange * frequency_hz / (sample_rate == 0 ? 1 : sample_rate))),
      total_frames_(static_cast<std::uint32_t>(static_cast<std::uint64_t>(duration_ms) * sample_rate / 1000)),
      fade_frames_(sample_rate * kFadeMs / 1000), peak_(kFullScale * (amplitude > 100 ? 100 : amplitude) / 100),
      waveform_(waveform)
{
    for (std::size_t index = 0; index < kTableSize; ++index)
        sine_[index] = static_cast<std::int16_t>(
            std::lround(std::sin(kTwoPi * static_cast<float>(index) / kTableSize) * kFullScale));
    if (fade_frames_ * 2 > total_frames_)
        fade_frames_ = total_frames_ / 2;
}

Status ToneSource::read(std::int16_t* frames, std::size_t capacity, std::size_t& count)
{
    count = 0;
    while (count < capacity && frame_ < total_frames_)
    {
        frames[count++] = static_cast<std::int16_t>(sample_(phase_) * envelope_(frame_) / kFullScale);
        phase_ += phase_step_;
        ++frame_;
    }
    return count == 0 ? Status::no_data : Status::ok;
}

Status ToneSource::rewind()
{
    frame_ = 0;
    phase_ = 0;
    return Status::ok;
}

std::int16_t ToneSource::sample_(std::uint32_t phase) const
{
    if (waveform_ == Waveform::square)
        return static_cast<std::int16_t>((phase & 0x80000000U) != 0 ? -kFullScale : kFullScale);
    return sine_[phase >> kPhaseBits];
}

std::int32_t ToneSource::envelope_(std::uint32_t frame) const
{
    std::int32_t level = peak_;
    if (fade_frames_ > 0 && frame < fade_frames_)
        level = static_cast<std::int32_t>(static_cast<std::int64_t>(peak_) * frame / fade_frames_);
    const std::uint32_t left = total_frames_ - frame;
    if (fade_frames_ > 0 && left < fade_frames_)
        level = static_cast<std::int32_t>(static_cast<std::int64_t>(peak_) * left / fade_frames_);
    return level;
}

// ---- MemoryBytes ------------------------------------------------------------------

Status MemoryBytes::read(std::uint32_t address, void* data, std::size_t size)
{
    if (data == nullptr || address > size_ || size > size_ - address)
        return Status::invalid_argument;
    std::memcpy(data, data_ + address, size);
    return Status::ok;
}

// ---- WavSource ------------------------------------------------------------------------

WavSource::WavSource(interfaces::storage::IByteStorage& storage, std::uint32_t address, std::uint32_t size)
    : storage_(storage), address_(address), size_(size)
{
}

Status WavSource::open()
{
    format_ = { 0, 1 };
    std::uint8_t header[kRiffHeader];
    if (const Status status = storage_.read(address_, header, sizeof(header)); !is_ok(status))
        return status;
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
        return Status::error;
    const std::uint32_t end         = size_ != 0 ? size_ : le32(header + 4) + kChunkHeader;
    bool                have_format = false;
    for (std::uint32_t offset = kRiffHeader; offset + kChunkHeader <= end;)
    {
        std::uint8_t chunk[kChunkHeader + 16];
        if (const Status status = storage_.read(address_ + offset, chunk, kChunkHeader); !is_ok(status))
            return status;
        const std::uint32_t chunk_size = le32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0)
        {
            if (chunk_size < 16)
                return Status::error;
            if (const Status status = storage_.read(address_ + offset + kChunkHeader, chunk + kChunkHeader, 16);
                !is_ok(status))
                return status;
            const std::uint16_t encoding = le16(chunk + 8);
            const std::uint16_t channels = le16(chunk + 10);
            const std::uint16_t bits     = le16(chunk + 22);
            if (encoding != kPcmEncoding || (channels != 1 && channels != 2) || (bits != 8 && bits != 16))
                return Status::not_supported;
            format_           = { le32(chunk + 12), static_cast<std::uint8_t>(channels) };
            bytes_per_sample_ = static_cast<std::uint8_t>(bits / 8);
            have_format       = true;
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            if (!have_format || format_.sample_rate == 0)
                return Status::error;
            data_offset_ = offset + kChunkHeader;
            data_size_   = size_ != 0 && data_offset_ + chunk_size > size_ ? size_ - data_offset_ : chunk_size;
            position_    = 0;
            return Status::ok;
        }
        offset += kChunkHeader + chunk_size + (chunk_size & 1U);
    }
    return Status::error;
}

Status WavSource::read(std::int16_t* frames, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (format_.sample_rate == 0)
        return Status::not_ready;
    const std::uint32_t frame_bytes = static_cast<std::uint32_t>(bytes_per_sample_) * format_.channels;
    const std::uint32_t left        = (data_size_ - position_) / frame_bytes;
    if (left == 0)
        return Status::no_data;
    const std::uint32_t wanted  = capacity < left ? static_cast<std::uint32_t>(capacity) : left;
    const std::size_t   samples = static_cast<std::size_t>(wanted) * format_.channels;
    if (bytes_per_sample_ == 2)
    {
        // Little-endian, as the ESP32, the STM32 and the PC.
        if (const Status status = storage_.read(address_ + data_offset_ + position_, frames, samples * 2);
            !is_ok(status))
            return status;
    }
    else
    {
        // 8-bit unsigned: read into the upper half, widen from the front.
        auto* const bytes = reinterpret_cast<std::uint8_t*>(frames) + samples;
        if (const Status status = storage_.read(address_ + data_offset_ + position_, bytes, samples); !is_ok(status))
            return status;
        for (std::size_t index = 0; index < samples; ++index)
            frames[index] = static_cast<std::int16_t>((static_cast<std::int16_t>(bytes[index]) - 128) * 256);
    }
    position_ += wanted * frame_bytes;
    count = wanted;
    return Status::ok;
}

Status WavSource::rewind()
{
    position_ = 0;
    return format_.sample_rate == 0 ? Status::not_ready : Status::ok;
}

std::uint32_t WavSource::duration_ms() const
{
    const std::uint32_t frame_bytes = static_cast<std::uint32_t>(bytes_per_sample_) * format_.channels;
    return format_.sample_rate == 0 ? 0 :
                                      static_cast<std::uint32_t>(static_cast<std::uint64_t>(data_size_ / frame_bytes)
                                                                 * 1000 / format_.sample_rate);
}

// ---- PcmPlayer ------------------------------------------------------------------------

Status PcmPlayer::play(IPcmSource& source)
{
    const interfaces::audio::PcmFormat wanted = source.format();
    if (wanted.sample_rate == 0 || wanted.channels == 0 || wanted.channels > 2)
        return Status::invalid_argument;
    if (output_.format() != wanted)
        if (const Status status = output_.start(wanted); !is_ok(status))
            return status;
    source_       = &source;
    channels_     = wanted.channels;
    chunk_frames_ = 0;
    sent_frames_  = 0;
    update();
    return Status::ok;
}

void PcmPlayer::stop()
{
    source_       = nullptr;
    chunk_frames_ = 0;
    sent_frames_  = 0;
}

void PcmPlayer::update()
{
    while (source_ != nullptr)
    {
        if (sent_frames_ == chunk_frames_)
        {
            std::size_t  count  = 0;
            const Status status = source_->read(chunk_.data(), kChunkFrames, count);
            if (!is_ok(status) || count == 0)
            {
                stop();
                return;
            }
            chunk_frames_ = count;
            sent_frames_  = 0;
        }
        std::size_t  written = 0;
        const Status status =
            output_.write(chunk_.data() + sent_frames_ * channels_, chunk_frames_ - sent_frames_, written);
        sent_frames_ += written;
        frames_sent_ += written;
        if (!is_ok(status) || written == 0)
            return;  // the output's buffers are full: later
    }
}

}  // namespace brick::core::audio
