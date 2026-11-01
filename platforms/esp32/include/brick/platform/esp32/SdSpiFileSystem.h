#pragma once

#include <array>
#include <cstddef>

#include "brick/interfaces/storage/IRemovableFileSystem.h"
#include "brick/platform/esp32/File.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace brick::platform::esp32
{

struct SdSpiFileSystemConfig
{
    gpio_num_t        cs          = GPIO_NUM_NC;
    gpio_num_t        sck         = GPIO_NUM_NC;
    gpio_num_t        mosi        = GPIO_NUM_NC;
    gpio_num_t        miso        = GPIO_NUM_NC;
    spi_host_device_t host        = SPI2_HOST;
    const char*       mount_point = "/sdcard";
    // 20 MHz is stable with the tested panel/card combination.
    int max_freq_khz = 20000;
};

class SdSpiFileSystem final : public interfaces::storage::IRemovableFileSystem
{
public:
    explicit SdSpiFileSystem(SdSpiFileSystemConfig config);
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
    SdSpiFileSystemConfig           config_;
    std::array<File, kMaxOpenFiles> files_{};
    bool                            mounted_         = false;
    bool                            bus_initialized_ = false;
    sdmmc_card_t*                   card_            = nullptr;
};

}  // namespace brick::platform::esp32
