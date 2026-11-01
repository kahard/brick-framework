#include "brick/platform/esp32/FileSystem.h"

#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/VfsDirectory.h"

namespace brick::platform::esp32
{

Status FileSystem::mount()
{
    if (mounted_)
        return Status::ok;
    sdmmc_host_t host                    = SDSPI_HOST_DEFAULT();
    host.slot                            = SPI2_HOST;
    sdspi_device_config_t slot_cfg       = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id                     = SPI2_HOST;
    slot_cfg.gpio_cs                     = GPIO_NUM_42;
    esp_vfs_fat_mount_config_t mount_cfg = { .format_if_mount_failed = false,
                                             .max_files              = kMaxOpenFiles,
                                             .allocation_unit_size   = 16 * 1024 };
    const esp_err_t            result    = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_cfg, &mount_cfg, &card_);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "SD mount: %s", esp_err_to_name(result));
        return to_status(result);
    }
    mounted_ = true;
    ESP_LOGI(TAG, "SD mounted at /sdcard");
    return Status::ok;
}

std::size_t FileSystem::list_files(const char* path, interfaces::storage::FileEntry* entries, std::size_t capacity)
{
    return list_vfs_files(path, entries, capacity);
}

Status FileSystem::open(const char* path, const char* mode, interfaces::storage::FileHandle& file)
{
    return open_vfs_file(files_, *this, path, mode, file);
}

void FileSystem::close(interfaces::storage::IFile& file)
{
    static_cast<File&>(file).close();
}

}  // namespace brick::platform::esp32
