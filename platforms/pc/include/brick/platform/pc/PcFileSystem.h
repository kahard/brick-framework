#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>

#include "brick/interfaces/storage/IRemovableFileSystem.h"

namespace brick::platform::pc
{

/// File opened with the C standard library; PcFileSystem keeps a fixed set of
/// them for open(), like the target file systems.
class PcFile final : public brick::interfaces::storage::IFile
{
public:
    PcFile() = default;
    ~PcFile() override;
    PcFile(const PcFile&)            = delete;
    PcFile& operator=(const PcFile&) = delete;

    Status open(const std::string& host_path, const char* mode);
    void   close();
    bool   is_open() const { return file_ != nullptr; }

    std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
    std::size_t write(const void* buffer, std::size_t size, std::size_t count) override;
    Status      seek(long offset, int origin) override;

private:
    FILE* file_ = nullptr;
};

/// A directory on the host standing in for an SD card or flash file system.
/// Target paths such as "/sdcard/assets.bin" are mapped below root by removing
/// mount_point ("/sdcard") first.
class PcFileSystem final : public brick::interfaces::storage::IRemovableFileSystem
{
public:
    explicit PcFileSystem(std::string root, std::string mount_point = "/sdcard");

    Status      mount() override;
    std::size_t list_files(const char* path, brick::interfaces::storage::FileEntry* entries,
                           std::size_t capacity) override;
    Status      open(const char* path, const char* mode, brick::interfaces::storage::FileHandle& file) override;

    bool        mounted() const override { return mounted_; }
    void        unmount() override { mounted_ = false; }
    bool        probe(const char* path) override;
    std::string host_path(const char* path) const;

    /// As many open files as the ESP32 FAT mounts allow.
    static constexpr std::size_t kMaxOpenFiles = 5;

protected:
    void close(brick::interfaces::storage::IFile& file) override;

private:
    std::string                       root_;
    std::string                       mount_point_;
    bool                              mounted_ = false;
    std::array<PcFile, kMaxOpenFiles> files_{};
};

}  // namespace brick::platform::pc
