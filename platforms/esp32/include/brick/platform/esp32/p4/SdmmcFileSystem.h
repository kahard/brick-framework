#pragma once

#include <cstdio>
#include <dirent.h>
#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/storage/IFileSystem.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace brick::platform::esp32
{

class SdmmcFile final : public interfaces::storage::IFile
{
public:
    explicit SdmmcFile(std::FILE* handle);
    ~SdmmcFile() override;
    std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
    std::size_t write(const void* buffer, std::size_t size, std::size_t count) override;
    bool        seek(long offset, int origin) override;

private:
    std::FILE* handle_;
};

class SdmmcFileSystem final : public interfaces::storage::IFileSystem
{
public:
    bool                                        mount() override;
    std::vector<std::string>                    list_files(const char* path) override;
    std::unique_ptr<interfaces::storage::IFile> open(const char* path, const char* mode) override;

private:
    static constexpr const char* TAG      = "brick_sdmmc";
    bool                         mounted_ = false;
    sdmmc_card_t*                card_    = nullptr;
};

}  // namespace brick::platform::esp32
