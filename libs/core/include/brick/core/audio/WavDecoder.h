#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/core/audio/AudioBuffer.h"
#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::core::audio
{

/// Decodes PCM WAV (8 or 16 bit, mono or stereo) to 8-bit unsigned mono.
class WavDecoder
{
public:
    /// Writes the samples to samples (capacity bytes, one per frame) and points
    /// output at them; samples must stay valid while output is played.
    /// Status::not_supported for a WAV other than 8/16-bit PCM mono/stereo,
    /// Status::no_memory when the file has more frames than capacity.
    static Status decode(interfaces::storage::IFileSystem& file_system, const char* path, std::uint8_t* samples,
                         std::size_t capacity, AudioBuffer& output);

private:
    static bool          read_exact(interfaces::storage::IFile& file, void* data, std::size_t size);
    static std::uint16_t read_u16(const std::uint8_t* data);
    static std::uint32_t read_u32(const std::uint8_t* data);
};

}  // namespace brick::core::audio
