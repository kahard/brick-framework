#include "brick/core/audio/WavDecoder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace brick::core::audio
{

bool WavDecoder::decode(interfaces::storage::IFileSystem& file_system, const char* path, AudioBuffer& output)
{
    auto file = file_system.open(path, "rb");
    if (!file)
        return false;

    std::uint8_t header[12];
    if (!read_exact(*file, header, sizeof(header)) || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
        return false;

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
            std::vector<std::uint8_t> fmt(chunk_size);
            if (fmt.size() < 16 || !read_exact(*file, fmt.data(), fmt.size()))
                return false;
            format      = read_u16(fmt.data());
            channels    = read_u16(fmt.data() + 2);
            sample_rate = read_u32(fmt.data() + 4);
            bits        = read_u16(fmt.data() + 14);
            position += static_cast<long>(chunk_size);
            have_fmt = true;
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            data_offset = position;
            data_size   = chunk_size;
            if (!file->seek(static_cast<long>(chunk_size), SEEK_CUR))
                return false;
            position += static_cast<long>(chunk_size);
            have_data = true;
        }
        else
        {
            if (!file->seek(static_cast<long>(chunk_size), SEEK_CUR))
                return false;
            position += static_cast<long>(chunk_size);
        }
        if (chunk_size & 1U)
        {
            if (!file->seek(1, SEEK_CUR))
                return false;
            ++position;
        }
        if (have_fmt && have_data)
            break;
    }

    if (!have_fmt || !have_data || format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || sample_rate == 0 || data_size == 0)
        return false;

    const std::size_t bytes_per_sample = bits / 8;
    const std::size_t bytes_per_frame  = channels * bytes_per_sample;
    const std::size_t frame_count      = data_size / bytes_per_frame;
    if (frame_count == 0 || !file->seek(data_offset, SEEK_SET))
        return false;

    std::vector<std::uint8_t> samples(frame_count), input(4096);
    std::size_t               remaining = frame_count, output_index = 0;
    while (remaining > 0)
    {
        const std::size_t frames = std::min(remaining, input.size() / bytes_per_frame);
        if (!read_exact(*file, input.data(), frames * bytes_per_frame))
            return false;
        for (std::size_t i = 0; i < frames; ++i)
        {
            int         mixed = 0;
            const auto* frame = input.data() + i * bytes_per_frame;
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                const auto* sample = frame + channel * bytes_per_sample;
                mixed += bits == 8 ? static_cast<int>(sample[0]) - 128 : static_cast<std::int16_t>(read_u16(sample));
            }
            mixed                   = bits == 8 ? mixed / static_cast<int>(channels) + 128 : 128 + (mixed / static_cast<int>(channels) >> 8);
            samples[output_index++] = static_cast<std::uint8_t>(std::clamp(mixed, 0, 255));
        }
        remaining -= frames;
    }

    output.assign(std::move(samples), sample_rate);
    return true;
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
