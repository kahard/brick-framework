#pragma once

#include <array>
#include <cstddef>

#include "brick/interfaces/storage/IFileSystem.h"
#include "brick/platform/esp32/File.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace brick::platform::esp32
{

class FileSystem final : public interfaces::storage::IFileSystem
{
public:
    Status      mount() override;
    std::size_t list_files(const char* path, interfaces::storage::FileEntry* entries, std::size_t capacity) override;
    Status      open(const char* path, const char* mode, interfaces::storage::FileHandle& file) override;

    /// Matches max_files of the FAT mount.
    static constexpr std::size_t kMaxOpenFiles = 5;

protected:
    void close(interfaces::storage::IFile& file) override;

private:
    static constexpr const char*    TAG      = "brick_fs";
    bool                            mounted_ = false;
    sdmmc_card_t*                   card_    = nullptr;
    std::array<File, kMaxOpenFiles> files_{};
};

}  // namespace brick::platform::esp32
