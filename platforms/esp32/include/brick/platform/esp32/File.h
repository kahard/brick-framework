#pragma once

#include "brick/Status.h"
#include "brick/interfaces/storage/IFile.h"
#include <cstdio>

namespace brick::platform::esp32
{

/// File of the ESP-IDF VFS; a file system keeps a fixed set of them and hands
/// one out per open().
class File final : public interfaces::storage::IFile
{
public:
    File() = default;
    ~File() override;
    File(const File&)            = delete;
    File& operator=(const File&) = delete;

    Status open(const char* path, const char* mode);
    void   close();
    bool   is_open() const { return handle_ != nullptr; }

    std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
    std::size_t write(const void* buffer, std::size_t size, std::size_t count) override;
    Status      seek(long offset, int origin) override;

private:
    std::FILE* handle_ = nullptr;
};

}  // namespace brick::platform::esp32
