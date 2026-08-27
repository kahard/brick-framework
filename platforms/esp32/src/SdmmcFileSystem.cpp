#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

namespace brick::platform::esp32
{

SdmmcFile::SdmmcFile(std::FILE* handle) : handle_(handle)
{
}

SdmmcFile::~SdmmcFile()
{
    if (handle_ != nullptr)
        std::fclose(handle_);
}

std::size_t SdmmcFile::read(void* buffer, std::size_t size, std::size_t count)
{
    return handle_ == nullptr ? 0 : std::fread(buffer, size, count, handle_);
}

std::size_t SdmmcFile::write(const void* buffer, std::size_t size, std::size_t count)
{
    return handle_ == nullptr ? 0 : std::fwrite(buffer, size, count, handle_);
}

bool SdmmcFile::seek(long offset, int origin)
{
    return handle_ != nullptr && std::fseek(handle_, offset, origin) == 0;
}

bool SdmmcFileSystem::mount()
{
    if (mounted_)
        return true;
    // JC1060 switches the TF-card 3.3 V rail through an active-low GPIO45 gate.
    gpio_set_direction(GPIO_NUM_45, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_45, 0);
    esp_rom_delay_us(10000);
    sdmmc_host_t host                       = SDMMC_HOST_DEFAULT();
    host.max_freq_khz                       = 10000;
    sdmmc_slot_config_t slot                = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width                              = 1;
    slot.clk                                = GPIO_NUM_43;
    slot.cmd                                = GPIO_NUM_44;
    slot.d0                                 = GPIO_NUM_39;
    slot.d1                                 = GPIO_NUM_40;
    slot.d2                                 = GPIO_NUM_41;
    slot.d3                                 = GPIO_NUM_42;
    slot.flags                              = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t config = { .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024, .disk_status_check_enable = false, .use_one_fat = false };
    const esp_err_t                  result = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &config, &card_);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "SDMMC mount failed: %s", esp_err_to_name(result));
        return false;
    }
    mounted_ = true;
    ESP_LOGI(TAG, "SDMMC mounted at /sdcard");
    return true;
}

std::vector<std::string> SdmmcFileSystem::list_files(const char* path)
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

std::unique_ptr<interfaces::storage::IFile> SdmmcFileSystem::open(const char* path, const char* mode)
{
    std::FILE* handle = std::fopen(path, mode);
    return handle == nullptr ? nullptr : std::make_unique<SdmmcFile>(handle);
}

}  // namespace brick::platform::esp32
