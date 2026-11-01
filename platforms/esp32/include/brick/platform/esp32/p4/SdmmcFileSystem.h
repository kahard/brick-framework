#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "brick/interfaces/storage/IRemovableFileSystem.h"
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

/// File on the SD card; SdmmcFileSystem keeps a fixed set of them for open().
class SdmmcFile final : public interfaces::storage::IFile
{
public:
    SdmmcFile() = default;
    ~SdmmcFile() override;
    SdmmcFile(const SdmmcFile&)            = delete;
    SdmmcFile& operator=(const SdmmcFile&) = delete;

    Status      open(const char* path, const char* mode);
    void        close();
    bool        is_open() const { return handle_ != nullptr; }
    std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
    std::size_t write(const void* buffer, std::size_t size, std::size_t count) override;
    Status      seek(long offset, int origin) override;

private:
    std::FILE* handle_ = nullptr;
};

class SdmmcFileSystem final : public interfaces::storage::IRemovableFileSystem
{
public:
    explicit SdmmcFileSystem(SdmmcFileSystemConfig config = {}) : config_(config) {}
    Status      mount() override;
    void        unmount() override;
    bool        mounted() const override { return mounted_; }
    bool        probe(const char* path) override;
    std::size_t list_files(const char* path, interfaces::storage::FileEntry* entries, std::size_t capacity) override;
    Status      open(const char* path, const char* mode, interfaces::storage::FileHandle& file) override;

    /// Matches max_files of the FAT mount.
    static constexpr std::size_t kMaxOpenFiles = 5;

protected:
    void close(interfaces::storage::IFile& file) override;

private:
    static constexpr const char*         TAG              = "brick_sdmmc";
    bool                                 mounted_         = false;
    sdmmc_card_t*                        card_            = nullptr;
    sd_pwr_ctrl_handle_t                 pwr_ctrl_handle_ = nullptr;
    SdmmcFileSystemConfig                config_{};
    std::array<SdmmcFile, kMaxOpenFiles> files_{};
};

}  // namespace brick::platform::esp32
