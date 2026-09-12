#pragma once

#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/storage/IFileSystem.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl.h"
#include "sdmmc_cmd.h"

namespace brick::platform::esp32
{

struct SdmmcFileSystemConfig
{
    gpio_num_t    clk          = GPIO_NUM_43;
    gpio_num_t    cmd          = GPIO_NUM_44;
    gpio_num_t    d0           = GPIO_NUM_39;
    gpio_num_t    d1           = GPIO_NUM_40;
    gpio_num_t    d2           = GPIO_NUM_41;
    gpio_num_t    d3           = GPIO_NUM_42;
    std::uint32_t max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    std::uint8_t  bus_width    = 4;
    int           host_slot    = SDMMC_HOST_SLOT_0;
};

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
    explicit SdmmcFileSystem(SdmmcFileSystemConfig config = {}) : config_(config) {}
    bool                                        mount() override;
    void                                        unmount();
    bool                                        mounted() const { return mounted_; }
    bool                                        probe(const char* path);
    std::vector<std::string>                    list_files(const char* path) override;
    std::unique_ptr<interfaces::storage::IFile> open(const char* path, const char* mode) override;

private:
    static constexpr const char* TAG              = "brick_sdmmc";
    bool                         mounted_         = false;
    sdmmc_card_t*                card_            = nullptr;
    sd_pwr_ctrl_handle_t         pwr_ctrl_handle_ = nullptr;
    SdmmcFileSystemConfig        config_{};
};

}  // namespace brick::platform::esp32
