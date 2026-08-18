#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::core::image
{

struct BmpDecodeOptions
{
    std::uint32_t target_width;
    std::uint32_t target_height;
    void (*on_row)(std::uint32_t row) = nullptr;
};

class BmpDecoder
{
public:
    static bool decode(interfaces::storage::IFileSystem& file_system, const char* path, std::uint8_t* destination, const BmpDecodeOptions& options);

private:
    static std::uint16_t read_u16(const std::uint8_t* value);
    static std::uint32_t read_u32(const std::uint8_t* value);
    static std::int32_t  read_i32(const std::uint8_t* value);
};

}  // namespace brick::core::image
