#include "brick/platform/esp32/FileSystem.h"

namespace brick::platform::esp32
{

bool FileSystem::mount()
{
    if (mounted_)
        return true;
    sdmmc_host_t host                    = SDSPI_HOST_DEFAULT();
    host.slot                            = SPI2_HOST;
    sdspi_device_config_t slot_cfg       = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.host_id                     = SPI2_HOST;
    slot_cfg.gpio_cs                     = GPIO_NUM_42;
    esp_vfs_fat_mount_config_t mount_cfg = { .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024 };
    const esp_err_t            result    = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_cfg, &mount_cfg, &card_);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "SD mount: %s", esp_err_to_name(result));
        return false;
    }
    mounted_ = true;
    ESP_LOGI(TAG, "SD mounted at /sdcard");
    return true;
}

std::vector<std::string> FileSystem::list_files(const char* path)
{
    std::vector<std::string> files;
    DIR*                     directory = opendir(path);
    if (directory == nullptr)
        return files;
    struct dirent* entry;
    while ((entry = readdir(directory)) != nullptr)
    {
        if (entry->d_type == DT_REG)
            files.emplace_back(std::string(path) + "/" + entry->d_name);
    }
    closedir(directory);
    return files;
}

std::unique_ptr<interfaces::storage::IFile> FileSystem::open(const char* path, const char* mode)
{
    std::FILE* handle = std::fopen(path, mode);
    return handle == nullptr ? nullptr : std::unique_ptr<interfaces::storage::IFile>(new File(handle));
}

}  // namespace brick::platform::esp32
