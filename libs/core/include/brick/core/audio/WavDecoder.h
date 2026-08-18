#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/core/audio/AudioBuffer.h"
#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::core::audio
{

class WavDecoder
{
public:
    static bool decode(interfaces::storage::IFileSystem& file_system, const char* path, AudioBuffer& output);

private:
    static bool          read_exact(interfaces::storage::IFile& file, void* data, std::size_t size);
    static std::uint16_t read_u16(const std::uint8_t* data);
    static std::uint32_t read_u32(const std::uint8_t* data);
};

}  // namespace brick::core::audio
