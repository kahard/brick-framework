#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#include <cerrno>
#include <cstring>
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

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
    sdmmc_host_t host                       = SDMMC_HOST_DEFAULT();
    host.slot                                = SDMMC_HOST_SLOT_0;
    host.max_freq_khz                        = SDMMC_FREQ_HIGHSPEED;
    sd_pwr_ctrl_ldo_config_t ldo_config      = { .ldo_chan_id = 4 };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle     = nullptr;
    const esp_err_t power_result             = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (power_result != ESP_OK)
    {
        ESP_LOGE(TAG, "SDMMC power control failed: %s", esp_err_to_name(power_result));
        return false;
    }
    host.pwr_ctrl_handle                    = pwr_ctrl_handle;
    sdmmc_slot_config_t slot                = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width                              = 4;
    slot.cd                                 = SDMMC_SLOT_NO_CD;
    slot.wp                                 = SDMMC_SLOT_NO_WP;
    slot.flags                              = 0;
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
    if (handle == nullptr)
    {
        ESP_LOGE(TAG, "fopen failed path=%s mode=%s errno=%d (%s)", path, mode, errno, std::strerror(errno));
        return nullptr;
    }
    return std::make_unique<SdmmcFile>(handle);
}

}  // namespace brick::platform::esp32
