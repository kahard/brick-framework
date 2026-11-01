#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::core::image
{

struct BmpDecodeOptions
{
    std::uint32_t target_width;
    std::uint32_t target_height;
    void (*on_row)(std::uint32_t row) = nullptr;
};

/// Decodes a 24- or 32-bit BMP into RGB565 (little endian), centred in a
/// target of target_width x target_height pixels; destination holds
/// target_width * target_height * 2 bytes. Larger images are cropped around
/// their centre. Images and targets are limited to 16384 x 16384 pixels;
/// Status::not_supported for other pixel formats and larger images.
class BmpDecoder final
{
public:
    static Status decode(interfaces::storage::IFileSystem& file_system, const char* path, std::uint8_t* destination,
                         const BmpDecodeOptions& options);

private:
    /// Where the copied part of the image is in the file and in the target.
    struct Layout
    {
        std::uint32_t pixel_offset    = 0;
        bool          bottom_up       = false;
        int           bytes_per_pixel = 0;
        int           stride          = 0;
        int           copy_width      = 0;
        int           copy_height     = 0;
        int           x_offset        = 0;
        int           y_offset        = 0;
        int           skip_rows       = 0;
    };

    static Status        read_layout(interfaces::storage::IFile& file, const BmpDecodeOptions& options, Layout& layout);
    static Status        copy_row(interfaces::storage::IFile& file, const Layout& layout, std::uint8_t* row);
    static std::uint16_t read_u16(const std::uint8_t* value);
    static std::uint32_t read_u32(const std::uint8_t* value);
    static std::int32_t  read_i32(const std::uint8_t* value);
};

}  // namespace brick::core::image
