#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::core::storage
{

// Performs one bounded write -> close -> read -> compare cycle. The helper is
// backend-neutral and therefore works for USB MSC, SD/MMC and host filesystems.
inline bool write_verify(brick::interfaces::storage::IFileSystem& file_system, const char* path, const std::uint8_t* pattern, std::size_t size)
{
    if (path == nullptr || pattern == nullptr || size == 0U)
        return false;
    {
        auto file = file_system.open(path, "w");
        if (file == nullptr || file->write(pattern, 1U, size) != size)
            return false;
    }
    std::unique_ptr<brick::interfaces::storage::IFile> file = file_system.open(path, "r");
    if (file == nullptr)
        return false;
    std::uint8_t buffer[256]{};
    if (size > sizeof(buffer) || file->read(buffer, 1U, size) != size)
        return false;
    for (std::size_t i = 0; i < size; ++i)
        if (buffer[i] != pattern[i])
            return false;
    return true;
}

}  // namespace brick::core::storage
