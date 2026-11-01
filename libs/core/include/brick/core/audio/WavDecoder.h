#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/core/audio/AudioBuffer.h"
#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::core::audio
{

/// Decodes PCM WAV (8 or 16 bit, mono or stereo) to 8-bit unsigned mono.
class WavDecoder final
{
public:
    /// Writes the samples to samples (capacity bytes, one per frame) and points
    /// output at them; samples must stay valid while output is played.
    /// Status::not_supported for a WAV other than 8/16-bit PCM mono/stereo,
    /// Status::no_memory when the file has more frames than capacity.
    static Status decode(interfaces::storage::IFileSystem& file_system, const char* path, std::uint8_t* samples,
                         std::size_t capacity, AudioBuffer& output);

private:
    struct Format
    {
        std::uint16_t encoding    = 0;
        std::uint16_t channels    = 0;
        std::uint16_t bits        = 0;
        std::uint32_t sample_rate = 0;
        long          data_offset = 0;
        std::uint32_t data_size   = 0;

        std::size_t frame_bytes() const { return channels * (bits / 8U); }
    };

    static Status        read_format(interfaces::storage::IFile& file, Format& format);
    static Status        read_fmt_chunk(interfaces::storage::IFile& file, std::uint32_t size, Format& format);
    static Status        read_samples(interfaces::storage::IFile& file, const Format& format, std::size_t frame_count,
                                      std::uint8_t* samples);
    static std::uint8_t  mix_to_u8(const std::uint8_t* frame, const Format& format);
    static bool          read_exact(interfaces::storage::IFile& file, void* data, std::size_t size);
    static std::uint16_t read_u16(const std::uint8_t* data);
    static std::uint32_t read_u32(const std::uint8_t* data);
};

}  // namespace brick::core::audio
