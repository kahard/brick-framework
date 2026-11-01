#pragma once

#include <array>
#include <cstddef>

#include "brick/interfaces/storage/IRemovableFileSystem.h"
#include "brick/platform/esp32/SdSpiFileSystem.h"
#include "brick/platform/esp32/wroom32/CydSpi3PinMux.h"

namespace brick::platform::esp32::wroom32
{

class CydSdSpiFileSystem final : public brick::interfaces::storage::IRemovableFileSystem
{
public:
    CydSdSpiFileSystem(SdSpiFileSystemConfig config, CydSpi3PinMux& pin_mux);
    ~CydSdSpiFileSystem() override;

    Status      mount() override;
    void        unmount() override;
    bool        mounted() const override;
    bool        probe(const char* path) override;
    std::size_t list_files(const char* path, brick::interfaces::storage::FileEntry* entries,
                           std::size_t capacity) override;
    Status      open(const char* path, const char* mode, brick::interfaces::storage::FileHandle& file) override;

protected:
    void close(brick::interfaces::storage::IFile& file) override;

private:
    /// A file of the wrapped SD file system that selects the SD card on the
    /// shared SPI pins before every access.
    class File final : public brick::interfaces::storage::IFile
    {
    public:
        Status open(SdSpiFileSystem& filesystem, CydSpi3PinMux& pin_mux, const char* path, const char* mode);
        void   close();
        bool   is_open() const { return file_.is_open(); }

        std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
        std::size_t write(const void* buffer, std::size_t size, std::size_t count) override;
        Status      seek(long offset, int origin) override;

    private:
        brick::interfaces::storage::FileHandle file_;
        CydSpi3PinMux*                         pin_mux_ = nullptr;
    };

    CydSpi3PinMux&                                   pin_mux_;
    SdSpiFileSystem                                  filesystem_;
    std::array<File, SdSpiFileSystem::kMaxOpenFiles> files_{};
};

}  // namespace brick::platform::esp32::wroom32
