#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/storage/IFileSystem.h"
#include "brick/platform/stm32/f1/Stm32UsbHost.h"
#include "ff.h"

namespace brick::platform::stm32::f1
{

class Stm32UsbFile final : public brick::interfaces::storage::IFile
{
public:
    explicit Stm32UsbFile(FIL file) : file_(file) {}
    ~Stm32UsbFile() override { f_close(&file_); }
    std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
    std::size_t write(const void* buffer, std::size_t size, std::size_t count) override;
    bool seek(long offset, int origin) override;

private:
    FIL file_{};
};

class Stm32UsbFileSystem final : public brick::interfaces::storage::IFileSystem
{
public:
    explicit Stm32UsbFileSystem(Stm32UsbHost& host, std::uint32_t mount_timeout_ms = 10000)
        : host_(host), mount_timeout_ms_(mount_timeout_ms) {}

    bool mount() override;
    void unmount();
    bool mounted() const { return mounted_; }
    std::vector<std::string> list_files(const char* path) override;
    std::unique_ptr<brick::interfaces::storage::IFile> open(const char* path, const char* mode) override;

private:
    Stm32UsbHost& host_;
    std::uint32_t mount_timeout_ms_;
    FATFS fatfs_{};
    bool mounted_ = false;
};

}  // namespace brick::platform::stm32::f1
