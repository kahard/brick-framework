#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::core::storage
{

// Performs one bounded write -> close -> read -> compare cycle. The helper is
// backend-neutral and therefore works for USB MSC, SD/MMC and host filesystems.
// Status::error when the data read back differs from what was written.
inline Status write_verify(brick::interfaces::storage::IFileSystem& file_system, const char* path,
                           const std::uint8_t* pattern, std::size_t size)
{
    std::uint8_t buffer[256]{};
    if (path == nullptr || pattern == nullptr || size == 0U || size > sizeof(buffer))
        return Status::invalid_argument;
    brick::interfaces::storage::FileHandle file;
    if (const Status status = file_system.open(path, "w", file); !is_ok(status))
        return status;
    if (file->write(pattern, 1U, size) != size)
        return Status::error;
    file.close();
    if (const Status status = file_system.open(path, "r", file); !is_ok(status))
        return status;
    if (file->read(buffer, 1U, size) != size)
        return Status::error;
    for (std::size_t i = 0; i < size; ++i)
        if (buffer[i] != pattern[i])
            return Status::error;
    return Status::ok;
}

}  // namespace brick::core::storage
