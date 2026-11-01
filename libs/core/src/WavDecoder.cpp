#include "brick/core/audio/WavDecoder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace brick::core::audio
{

Status WavDecoder::decode(interfaces::storage::IFileSystem& file_system, const char* path, std::uint8_t* samples,
                          std::size_t capacity, AudioBuffer& output)
{
    if (samples == nullptr)
        return Status::invalid_argument;
    interfaces::storage::FileHandle file;
    if (const Status status = file_system.open(path, "rb", file); !is_ok(status))
        return status;

    std::uint8_t header[12];
    if (!read_exact(*file, header, sizeof(header)) || std::memcmp(header, "RIFF", 4) != 0
        || std::memcmp(header + 8, "WAVE", 4) != 0)
        return Status::error;

    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t sample_rate = 0, data_size = 0;
    bool          have_fmt = false, have_data = false;
    long          data_offset = 0, position = 12;

    while (true)
    {
        std::uint8_t chunk[8];
        if (!read_exact(*file, chunk, sizeof(chunk)))
            break;
        position += 8;
        const std::uint32_t chunk_size = read_u32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0)
        {
            // Only the first 16 bytes (PCM fields) are used; extensions are skipped.
            std::uint8_t fmt[16];
            if (chunk_size < sizeof(fmt) || !read_exact(*file, fmt, sizeof(fmt))
                || (chunk_size > sizeof(fmt)
                    && !is_ok(file->seek(static_cast<long>(chunk_size - sizeof(fmt)), SEEK_CUR))))
                return Status::error;
            format      = read_u16(fmt);
            channels    = read_u16(fmt + 2);
            sample_rate = read_u32(fmt + 4);
            bits        = read_u16(fmt + 14);
            position += static_cast<long>(chunk_size);
            have_fmt = true;
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            data_offset = position;
            data_size   = chunk_size;
            if (!is_ok(file->seek(static_cast<long>(chunk_size), SEEK_CUR)))
                return Status::error;
            position += static_cast<long>(chunk_size);
            have_data = true;
        }
        else
        {
            if (!is_ok(file->seek(static_cast<long>(chunk_size), SEEK_CUR)))
                return Status::error;
            position += static_cast<long>(chunk_size);
        }
        if (chunk_size & 1U)
        {
            if (!is_ok(file->seek(1, SEEK_CUR)))
                return Status::error;
            ++position;
        }
        if (have_fmt && have_data)
            break;
    }

    if (!have_fmt || !have_data || sample_rate == 0 || data_size == 0)
        return Status::error;
    if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16))
        return Status::not_supported;

    const std::size_t bytes_per_sample = bits / 8;
    const std::size_t bytes_per_frame  = channels * bytes_per_sample;
    const std::size_t frame_count      = data_size / bytes_per_frame;
    if (frame_count == 0)
        return Status::error;
    if (frame_count > capacity)
        return Status::no_memory;
    if (const Status status = file->seek(data_offset, SEEK_SET); !is_ok(status))
        return status;

    // 256 bytes hold a whole number of frames of every supported format.
    std::uint8_t input[256];
    std::size_t  remaining = frame_count, output_index = 0;
    while (remaining > 0)
    {
        const std::size_t frames = std::min(remaining, sizeof(input) / bytes_per_frame);
        if (!read_exact(*file, input, frames * bytes_per_frame))
            return Status::error;
        for (std::size_t i = 0; i < frames; ++i)
        {
            int         mixed = 0;
            const auto* frame = input + i * bytes_per_frame;
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                const auto* sample = frame + channel * bytes_per_sample;
                mixed += bits == 8 ? static_cast<int>(sample[0]) - 128 : static_cast<std::int16_t>(read_u16(sample));
            }
            mixed =
                bits == 8 ? mixed / static_cast<int>(channels) + 128 : 128 + (mixed / static_cast<int>(channels) >> 8);
            samples[output_index++] = static_cast<std::uint8_t>(std::clamp(mixed, 0, 255));
        }
        remaining -= frames;
    }

    output.assign(samples, frame_count, sample_rate);
    return Status::ok;
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
