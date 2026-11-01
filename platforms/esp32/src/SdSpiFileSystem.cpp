#include "brick/platform/esp32/SdSpiFileSystem.h"

#include <cstdio>

#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/VfsDirectory.h"
#include "driver/spi_common.h"
#include "esp_log.h"

namespace brick::platform::esp32
{
namespace
{
    constexpr char kTag[] = "brick_sd_spi";
}

SdSpiFileSystem::SdSpiFileSystem(SdSpiFileSystemConfig config) : config_(config)
{
}

void SdSpiFileSystem::unmount()
{
    if (mounted_)
    {
        esp_vfs_fat_sdcard_unmount(config_.mount_point, card_);
        mounted_ = false;
        card_    = nullptr;
    }
    if (bus_initialized_)
    {
        spi_bus_free(config_.host);
        bus_initialized_ = false;
    }
}

bool SdSpiFileSystem::probe(const char* path)
{
    if (!mounted_ || path == nullptr)
        return false;
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    unsigned char byte = 0;
    const bool    ok   = std::fread(&byte, 1, 1, file) == 1;
    std::fclose(file);
    return ok;
}

Status SdSpiFileSystem::mount()
{
    if (mounted_)
        return Status::ok;
    spi_bus_config_t bus = {};
    bus.mosi_io_num      = config_.mosi;
    bus.miso_io_num      = config_.miso;
    bus.sclk_io_num      = config_.sck;
    bus.quadwp_io_num    = -1;
    bus.quadhd_io_num    = -1;
    esp_err_t result     = spi_bus_initialize(config_.host, &bus, SDSPI_DEFAULT_DMA);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(kTag, "SPI bus init failed: %s", esp_err_to_name(result));
        return to_status(result);
    }
    bus_initialized_                        = result == ESP_OK;
    sdmmc_host_t host                       = SDSPI_HOST_DEFAULT();
    host.slot                               = config_.host;
    host.max_freq_khz                       = config_.max_freq_khz;
    sdspi_device_config_t device            = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id                          = config_.host;
    device.gpio_cs                          = config_.cs;
    esp_vfs_fat_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed     = false;
    mount_config.max_files                  = kMaxOpenFiles;
    mount_config.allocation_unit_size       = 16 * 1024;
    result = esp_vfs_fat_sdspi_mount(config_.mount_point, &host, &device, &mount_config, &card_);
    if (result != ESP_OK)
    {
        ESP_LOGE(kTag, "SD mount failed: %s", esp_err_to_name(result));
        return to_status(result);
    }
    mounted_ = true;
    ESP_LOGI(kTag, "SD mounted at %s", config_.mount_point);
    return Status::ok;
}

std::size_t SdSpiFileSystem::list_files(const char* path, interfaces::storage::FileEntry* entries, std::size_t capacity)
{
    return list_vfs_files(path, entries, capacity);
}

Status SdSpiFileSystem::open(const char* path, const char* mode, interfaces::storage::FileHandle& file)
{
    return open_vfs_file(files_, *this, path, mode, file);
}

void SdSpiFileSystem::close(interfaces::storage::IFile& file)
{
    static_cast<File&>(file).close();
}

}  // namespace brick::platform::esp32
