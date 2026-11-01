#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/VfsDirectory.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include <cerrno>
#include <cstring>

namespace brick::platform::esp32
{


void SdmmcFileSystem::unmount()
{
    if (mounted_)
    {
        esp_vfs_fat_sdcard_unmount("/sdcard", card_);
        mounted_ = false;
        card_    = nullptr;
    }
    if (pwr_ctrl_handle_ != nullptr)
    {
        sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle_);
        pwr_ctrl_handle_ = nullptr;
    }
}

bool SdmmcFileSystem::probe(const char* path)
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

SdmmcFile::~SdmmcFile()
{
    close();
}

Status SdmmcFile::open(const char* path, const char* mode)
{
    close();
    handle_ = std::fopen(path, mode);
    if (handle_ == nullptr)
        ESP_LOGE("brick_sdmmc", "fopen failed path=%s mode=%s errno=%d (%s)", path, mode, errno, std::strerror(errno));
    if (handle_ != nullptr)
        return Status::ok;
    return errno == ENOENT ? Status::not_found : Status::error;
}

void SdmmcFile::close()
{
    if (handle_ != nullptr)
        std::fclose(handle_);
    handle_ = nullptr;
}

std::size_t SdmmcFile::read(void* buffer, std::size_t size, std::size_t count)
{
    return handle_ == nullptr ? 0 : std::fread(buffer, size, count, handle_);
}

std::size_t SdmmcFile::write(const void* buffer, std::size_t size, std::size_t count)
{
    return handle_ == nullptr ? 0 : std::fwrite(buffer, size, count, handle_);
}

Status SdmmcFile::seek(long offset, int origin)
{
    if (handle_ == nullptr)
        return Status::not_ready;
    return std::fseek(handle_, offset, origin) == 0 ? Status::ok : Status::invalid_argument;
}

Status SdmmcFileSystem::mount()
{
    if (mounted_)
        return Status::ok;
    ESP_LOGI(TAG, "SDMMC slot=%d clk=%d cmd=%d d0=%d width=%u freq=%lu kHz", config_.host_slot, config_.clk,
             config_.cmd, config_.d0, static_cast<unsigned>(config_.bus_width),
             static_cast<unsigned long>(config_.max_freq_khz));
    sdmmc_host_t host                     = SDMMC_HOST_DEFAULT();
    host.slot                             = config_.host_slot;
    host.max_freq_khz                     = config_.max_freq_khz;
    sd_pwr_ctrl_ldo_config_t ldo_config   = { .ldo_chan_id = 4 };
    const esp_err_t          power_result = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle_);
    if (power_result != ESP_OK)
    {
        ESP_LOGE(TAG, "SDMMC power control failed: %s", esp_err_to_name(power_result));
        return to_status(power_result);
    }
    host.pwr_ctrl_handle                    = pwr_ctrl_handle_;
    sdmmc_slot_config_t slot                = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk                                = config_.clk;
    slot.cmd                                = config_.cmd;
    slot.d0                                 = config_.d0;
    slot.d1                                 = config_.d1;
    slot.d2                                 = config_.d2;
    slot.d3                                 = config_.d3;
    slot.width                              = config_.bus_width;
    slot.cd                                 = SDMMC_SLOT_NO_CD;
    slot.wp                                 = SDMMC_SLOT_NO_WP;
    slot.flags                              = 0;
    esp_vfs_fat_sdmmc_mount_config_t config = { .format_if_mount_failed   = false,
                                                .max_files                = kMaxOpenFiles,
                                                .allocation_unit_size     = 16 * 1024,
                                                .disk_status_check_enable = false,
                                                .use_one_fat              = false };
    const esp_err_t                  result = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &config, &card_);
    if (result != ESP_OK)
    {
        sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle_);
        pwr_ctrl_handle_ = nullptr;
        ESP_LOGE(TAG, "SDMMC mount failed: %s", esp_err_to_name(result));
        return to_status(result);
    }
    mounted_ = true;
    ESP_LOGI(TAG, "SDMMC mounted at /sdcard");
    return Status::ok;
}

std::size_t SdmmcFileSystem::list_files(const char* path, interfaces::storage::FileEntry* entries, std::size_t capacity)
{
    return list_vfs_files(path, entries, capacity);
}

Status SdmmcFileSystem::open(const char* path, const char* mode, interfaces::storage::FileHandle& file)
{
    return open_vfs_file(files_, *this, path, mode, file);
}

void SdmmcFileSystem::close(interfaces::storage::IFile& file)
{
    static_cast<SdmmcFile&>(file).close();
}

}  // namespace brick::platform::esp32
