#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/storage/IRemovableFileSystem.h"

namespace brick::platform::pc
{

/// File opened with the C standard library.
class PcFile final : public brick::interfaces::storage::IFile
{
public:
    explicit PcFile(FILE* file) : file_(file) {}
    ~PcFile() override;
    PcFile(const PcFile&)            = delete;
    PcFile& operator=(const PcFile&) = delete;

    std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
    std::size_t write(const void* buffer, std::size_t size, std::size_t count) override;
    bool        seek(long offset, int origin) override;

private:
    FILE* file_;
};

/// A directory on the host standing in for an SD card or flash file system.
/// Target paths such as "/sdcard/assets.bin" are mapped below root by removing
/// mount_point ("/sdcard") first.
class PcFileSystem final : public brick::interfaces::storage::IRemovableFileSystem
{
public:
    explicit PcFileSystem(std::string root, std::string mount_point = "/sdcard");

    bool                                               mount() override;
    std::vector<std::string>                           list_files(const char* path) override;
    std::unique_ptr<brick::interfaces::storage::IFile> open(const char* path, const char* mode) override;

    bool        mounted() const override { return mounted_; }
    void        unmount() override { mounted_ = false; }
    bool        probe(const char* path) override;
    std::string host_path(const char* path) const;

private:
    std::string root_;
    std::string mount_point_;
    bool        mounted_ = false;
};

}  // namespace brick::platform::pc
