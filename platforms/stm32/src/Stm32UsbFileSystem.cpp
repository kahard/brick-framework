#include "brick/platform/stm32/f1/Stm32UsbFileSystem.h"

#include <cstring>

#include "diskio.h"

namespace
{
brick::platform::stm32::f1::Stm32UsbHost* g_usb_host = nullptr;
}

extern "C" DSTATUS disk_initialize(BYTE) { return g_usb_host != nullptr && g_usb_host->storage_ready() ? 0 : STA_NOINIT; }
extern "C" DSTATUS disk_status(BYTE) { return g_usb_host != nullptr && g_usb_host->storage_ready() ? 0 : STA_NOINIT; }
extern "C" DRESULT disk_read(BYTE, BYTE* buffer, LBA_t sector, UINT count)
{
    return g_usb_host != nullptr && g_usb_host->read_blocks(static_cast<std::uint32_t>(sector), buffer, count) ? RES_OK : RES_NOTRDY;
}
extern "C" DRESULT disk_write(BYTE, const BYTE* buffer, LBA_t sector, UINT count)
{
    return g_usb_host != nullptr && g_usb_host->write_blocks(static_cast<std::uint32_t>(sector), const_cast<BYTE*>(buffer), count) ? RES_OK : RES_NOTRDY;
}
extern "C" DRESULT disk_ioctl(BYTE, BYTE, void*) { return RES_OK; }

namespace brick::platform::stm32::f1
{

std::size_t Stm32UsbFile::read(void* buffer, std::size_t size, std::size_t count)
{
    UINT bytes = 0;
    return f_read(&file_, buffer, static_cast<UINT>(size * count), &bytes) == FR_OK ? bytes / size : 0;
}

bool Stm32UsbFile::seek(long offset, int origin)
{
    FSIZE_t target = 0;
    if (origin == 0) target = static_cast<FSIZE_t>(offset);
    else if (origin == 1) target = f_tell(&file_) + static_cast<FSIZE_t>(offset);
    else if (origin == 2) target = f_size(&file_) + static_cast<FSIZE_t>(offset);
    else return false;
    return f_lseek(&file_, target) == FR_OK;
}

bool Stm32UsbFileSystem::mount()
{
    if (mounted_) return true;
    g_usb_host = &host_;
    const std::uint32_t start = HAL_GetTick();
    while (!host_.storage_ready() && HAL_GetTick() - start < mount_timeout_ms_)
    {
        host_.process();
        HAL_Delay(10);
    }
    if (!host_.storage_ready() || f_mount(&fatfs_, "", 1) != FR_OK)
        return false;
    mounted_ = true;
    return true;
}

std::vector<std::string> Stm32UsbFileSystem::list_files(const char* path)
{
    std::vector<std::string> result;
    if (!mounted_) return result;
    DIR dir{};
    FILINFO info{};
    if (f_opendir(&dir, path) != FR_OK) return result;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
        if (!(info.fattrib & AM_DIR)) result.emplace_back(info.fname);
    f_closedir(&dir);
    return result;
}

std::unique_ptr<brick::interfaces::storage::IFile> Stm32UsbFileSystem::open(const char* path, const char* mode)
{
    if (!mounted_) return nullptr;
    BYTE flags = FA_READ;
    if (std::strchr(mode, 'w') != nullptr) flags = FA_WRITE | FA_CREATE_ALWAYS;
    else if (std::strchr(mode, 'a') != nullptr) flags = FA_WRITE | FA_OPEN_APPEND;
    FIL file{};
    return f_open(&file, path, flags) == FR_OK ? std::make_unique<Stm32UsbFile>(file) : nullptr;
}

}  // namespace brick::platform::stm32::f1
