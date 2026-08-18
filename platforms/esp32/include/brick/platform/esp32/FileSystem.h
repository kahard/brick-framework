#pragma once

#include <dirent.h>
#include <memory>
#include <string>
#include <vector>

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
    bool                                        mount() override;
    std::vector<std::string>                    list_files(const char* path) override;
    std::unique_ptr<interfaces::storage::IFile> open(const char* path, const char* mode) override;

private:
    static constexpr const char* TAG      = "brick_fs";
    bool                         mounted_ = false;
    sdmmc_card_t*                card_    = nullptr;
};

}  // namespace brick::platform::esp32
