#pragma once

#include <cstddef>
#include <cstdio>
#include <dirent.h>

#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::platform::esp32
{

/// IRemovableFileSystem::probe() for a path of the ESP-IDF VFS: a file with
/// at least one byte, or a directory.
inline bool probe_vfs_path(const char* path)
{
    if (std::FILE* file = std::fopen(path, "rb"))
    {
        unsigned char byte = 0;
        const bool    read = std::fread(&byte, 1, 1, file) == 1;
        std::fclose(file);
        return read;
    }
    DIR* directory = opendir(path);
    if (directory == nullptr)
        return false;
    closedir(directory);
    return true;
}

/// IFileSystem::list_files() for a directory of the ESP-IDF VFS.
inline std::size_t list_vfs_files(const char* path, interfaces::storage::FileEntry* entries, std::size_t capacity)
{
    DIR* directory = opendir(path);
    if (directory == nullptr)
        return 0;
    std::size_t count = 0;
    while (const dirent* entry = readdir(directory))
    {
        if (entry->d_type != DT_REG)
            continue;
        if (entries != nullptr && count < capacity)
            entries[count].set_name(entry->d_name);
        ++count;
    }
    closedir(directory);
    return count;
}

/// IFileSystem::open() over a fixed set of file objects that provide
/// open(path, mode) and is_open().
/// Status::busy when every file object is open.
template <typename Files>
Status open_vfs_file(Files& files, interfaces::storage::IFileSystem& owner, const char* path, const char* mode,
                     interfaces::storage::FileHandle& handle)
{
    handle.close();
    for (auto& file : files)
    {
        if (file.is_open())
            continue;
        if (const Status status = file.open(path, mode); !is_ok(status))
            return status;
        handle.attach(owner, file);
        return Status::ok;
    }
    return Status::busy;
}

}  // namespace brick::platform::esp32
