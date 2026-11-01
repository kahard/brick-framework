#include "brick/core/audio/WavDecoder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace brick::core::audio
{

namespace
{

    constexpr std::uint16_t kPcmEncoding      = 1;
    constexpr long          kRiffHeaderBytes  = 12;
    constexpr long          kChunkHeaderBytes = 8;
    // Only the PCM fields of "fmt " are used; extensions are skipped.
    constexpr std::uint32_t kFmtBytes = 16;

}  // namespace

Status WavDecoder::decode(interfaces::storage::IFileSystem& file_system, const char* path, std::uint8_t* samples,
                          std::size_t capacity, AudioBuffer& output)
{
    if (samples == nullptr)
        return Status::invalid_argument;
    interfaces::storage::FileHandle file;
    if (const Status status = file_system.open(path, "rb", file); !is_ok(status))
        return status;

    Format format;
    if (const Status status = read_format(*file, format); !is_ok(status))
        return status;
    if (format.encoding != kPcmEncoding || (format.channels != 1 && format.channels != 2)
        || (format.bits != 8 && format.bits != 16))
        return Status::not_supported;

    const std::size_t frame_count = format.data_size / format.frame_bytes();
    if (frame_count == 0)
        return Status::error;
    if (frame_count > capacity)
        return Status::no_memory;
    if (const Status status = read_samples(*file, format, frame_count, samples); !is_ok(status))
        return status;

    output.assign(samples, frame_count, format.sample_rate);
    return Status::ok;
}

Status WavDecoder::read_format(interfaces::storage::IFile& file, Format& format)
{
    std::uint8_t header[kRiffHeaderBytes];
    if (!read_exact(file, header, sizeof(header)) || std::memcmp(header, "RIFF", 4) != 0
        || std::memcmp(header + 8, "WAVE", 4) != 0)
        return Status::error;

    bool         have_fmt = false, have_data = false;
    long         position = kRiffHeaderBytes;
    std::uint8_t chunk[kChunkHeaderBytes];
    while (!(have_fmt && have_data) && read_exact(file, chunk, sizeof(chunk)))
    {
        const std::uint32_t size = read_u32(chunk + 4);
        // Chunks are padded to an even size.
        const long padded_size = static_cast<long>(size + (size & 1U));
        long       to_skip     = padded_size;
        position += kChunkHeaderBytes;
        if (std::memcmp(chunk, "fmt ", 4) == 0)
        {
            if (const Status status = read_fmt_chunk(file, size, format); !is_ok(status))
                return status;
            to_skip -= kFmtBytes;
            have_fmt = true;
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            format.data_offset = position;
            format.data_size   = size;
            have_data          = true;
        }
        // The last chunk needed is not skipped: its pad byte may be missing.
        if (!(have_fmt && have_data) && to_skip > 0 && !is_ok(file.seek(to_skip, SEEK_CUR)))
            return Status::error;
        position += padded_size;
    }

    if (!have_fmt || !have_data || format.sample_rate == 0 || format.data_size == 0)
        return Status::error;
    return Status::ok;
}

Status WavDecoder::read_fmt_chunk(interfaces::storage::IFile& file, std::uint32_t size, Format& format)
{
    std::uint8_t fmt[kFmtBytes];
    if (size < sizeof(fmt) || !read_exact(file, fmt, sizeof(fmt)))
        return Status::error;
    format.encoding    = read_u16(fmt);
    format.channels    = read_u16(fmt + 2);
    format.sample_rate = read_u32(fmt + 4);
    format.bits        = read_u16(fmt + 14);
    return Status::ok;
}

Status WavDecoder::read_samples(interfaces::storage::IFile& file, const Format& format, std::size_t frame_count,
                                std::uint8_t* samples)
{
    if (const Status status = file.seek(format.data_offset, SEEK_SET); !is_ok(status))
        return status;

    // 256 bytes hold a whole number of frames of every supported format.
    std::uint8_t      input[256];
    const std::size_t frame_bytes = format.frame_bytes();
    for (std::size_t done = 0; done < frame_count;)
    {
        const std::size_t frames = std::min(frame_count - done, sizeof(input) / frame_bytes);
        if (!read_exact(file, input, frames * frame_bytes))
            return Status::error;
        for (std::size_t i = 0; i < frames; ++i)
            samples[done + i] = mix_to_u8(input + i * frame_bytes, format);
        done += frames;
    }
    return Status::ok;
}

std::uint8_t WavDecoder::mix_to_u8(const std::uint8_t* frame, const Format& format)
{
    const int channels = format.channels;
    int       mixed    = 0;
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* sample = frame + channel * (format.bits / 8);
        mixed += format.bits == 8 ? static_cast<int>(sample[0]) - 128 : static_cast<std::int16_t>(read_u16(sample));
    }
    // Average of signed samples back to unsigned 8 bit.
    mixed = format.bits == 8 ? mixed / channels + 128 : 128 + (mixed / channels >> 8);
    return static_cast<std::uint8_t>(std::clamp(mixed, 0, 255));
}

bool WavDecoder::read_exact(interfaces::storage::IFile& file, void* data, std::size_t size)
{
    return file.read(data, 1, size) == size;
}

std::uint16_t WavDecoder::read_u16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::uint32_t WavDecoder::read_u32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

}  // namespace brick::core::audio
