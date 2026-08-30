#include "brick/platform/esp32/SdSpiFileSystem.h"

#include <cstdio>

#include "esp_log.h"
#include "driver/spi_common.h"

namespace brick::platform::esp32 {
namespace {
constexpr char kTag[] = "brick_sd_spi";
}

SdSpiFileSystem::SdSpiFileSystem(SdSpiFileSystemConfig config) : config_(config) {}

void SdSpiFileSystem::unmount() {
    if (mounted_) {
        esp_vfs_fat_sdcard_unmount(config_.mount_point, card_);
        mounted_ = false;
        card_ = nullptr;
    }
    if (bus_initialized_) {
        spi_bus_free(config_.host);
        bus_initialized_ = false;
    }
}

bool SdSpiFileSystem::probe(const char* path) {
    if (!mounted_ || path == nullptr) return false;
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) return false;
    unsigned char byte = 0;
    const bool ok = std::fread(&byte, 1, 1, file) == 1;
    std::fclose(file);
    return ok;
}

bool SdSpiFileSystem::mount() {
    if (mounted_) return true;
    spi_bus_config_t bus = {};
    bus.mosi_io_num = config_.mosi;
    bus.miso_io_num = config_.miso;
    bus.sclk_io_num = config_.sck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    esp_err_t result = spi_bus_initialize(config_.host, &bus, SDSPI_DEFAULT_DMA);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "SPI bus init failed: %s", esp_err_to_name(result));
        return false;
    }
    bus_initialized_ = true;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = config_.host;
    host.max_freq_khz = config_.max_freq_khz;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = config_.host;
    device.gpio_cs = config_.cs;
    esp_vfs_fat_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;
    result = esp_vfs_fat_sdspi_mount(config_.mount_point, &host, &device, &mount_config, &card_);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "SD mount failed: %s", esp_err_to_name(result));
        return false;
    }
    mounted_ = true;
    ESP_LOGI(kTag, "SD mounted at %s", config_.mount_point);
    return true;
}

std::vector<std::string> SdSpiFileSystem::list_files(const char* path) {
    std::vector<std::string> files;
    DIR* directory = opendir(path);
    if (!directory) return files;
    struct dirent* entry;
    while ((entry = readdir(directory)) != nullptr)
        if (entry->d_type == DT_REG) files.emplace_back(std::string(path) + "/" + entry->d_name);
    closedir(directory);
    return files;
}

std::unique_ptr<interfaces::storage::IFile> SdSpiFileSystem::open(const char* path, const char* mode) {
    std::FILE* handle = std::fopen(path, mode);
    return handle == nullptr ? nullptr : std::unique_ptr<interfaces::storage::IFile>(new File(handle));
}

}  // namespace brick::platform::esp32
