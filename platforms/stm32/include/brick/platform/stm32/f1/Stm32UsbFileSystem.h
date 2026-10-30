#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/storage/IFileSystem.h"
#include "brick/platform/stm32/f1/Stm32UsbHost.h"
#include "ff.h"

// Each open file holds a FatFs FIL (with its sector buffer unless FF_FS_TINY).
#ifndef BRICK_STM32_USB_FS_MAX_OPEN_FILES
#    define BRICK_STM32_USB_FS_MAX_OPEN_FILES 2
#endif

namespace brick::platform::stm32::f1
{

/// FatFs file; Stm32UsbFileSystem keeps a fixed set of them for open().
class Stm32UsbFile final : public brick::interfaces::storage::IFile
{
public:
    Stm32UsbFile() = default;
    ~Stm32UsbFile() override { close(); }
    Stm32UsbFile(const Stm32UsbFile&)            = delete;
    Stm32UsbFile& operator=(const Stm32UsbFile&) = delete;

    bool        open(const char* path, const char* mode);
    void        close();
    bool        is_open() const { return open_; }
    std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
    std::size_t write(const void* buffer, std::size_t size, std::size_t count) override;
    bool        seek(long offset, int origin) override;

private:
    FIL  file_{};
    bool open_ = false;
};

class Stm32UsbFileSystem final : public brick::interfaces::storage::IFileSystem
{
public:
    explicit Stm32UsbFileSystem(Stm32UsbHost& host, std::uint32_t mount_timeout_ms = 10000)
        : host_(host), mount_timeout_ms_(mount_timeout_ms)
    {
    }

    bool        mount() override;
    void        unmount();
    bool        mounted() const { return mounted_; }
    std::size_t list_files(const char* path, brick::interfaces::storage::FileEntry* entries,
                           std::size_t capacity) override;
    bool        open(const char* path, const char* mode, brick::interfaces::storage::FileHandle& file) override;

    static constexpr std::size_t kMaxOpenFiles = BRICK_STM32_USB_FS_MAX_OPEN_FILES;

protected:
    void close(brick::interfaces::storage::IFile& file) override;

private:
    Stm32UsbHost&                           host_;
    std::uint32_t                           mount_timeout_ms_;
    FATFS                                   fatfs_{};
    bool                                    mounted_ = false;
    std::array<Stm32UsbFile, kMaxOpenFiles> files_{};
};

}  // namespace brick::platform::stm32::f1
