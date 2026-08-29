#pragma once

#include <dirent.h>
#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/storage/IFileSystem.h"
#include "brick/platform/esp32/File.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace brick::platform::esp32 {

struct SdSpiFileSystemConfig {
    gpio_num_t cs = GPIO_NUM_NC;
    gpio_num_t sck = GPIO_NUM_NC;
    gpio_num_t mosi = GPIO_NUM_NC;
    gpio_num_t miso = GPIO_NUM_NC;
    spi_host_device_t host = SPI2_HOST;
    const char* mount_point = "/sdcard";
    // 30 MHz is an intermediate speed between the stable 20 MHz and
    // unreliable 40 MHz setting.
    int max_freq_khz = 30000;
};

class SdSpiFileSystem final : public interfaces::storage::IFileSystem {
public:
    explicit SdSpiFileSystem(SdSpiFileSystemConfig config);
    bool mount() override;
    void unmount();
    std::vector<std::string> list_files(const char* path) override;
    std::unique_ptr<interfaces::storage::IFile> open(const char* path, const char* mode) override;

private:
    SdSpiFileSystemConfig config_;
    bool mounted_ = false;
    bool bus_initialized_ = false;
    sdmmc_card_t* card_ = nullptr;
};

}  // namespace brick::platform::esp32
