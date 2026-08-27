#include "brick/platform/stm32/f1/Stm32UsbFileSystem.h"

#include <cstring>

#include "brick/platform/stm32/f1/St280BoardConfig.h"
#include "diskio.h"

namespace
{
brick::platform::stm32::f1::Stm32UsbHost* g_usb_host = nullptr;
}

extern "C" DSTATUS disk_initialize(BYTE) { return g_usb_host != nullptr && g_usb_host->storage_ready() ? 0 : STA_NOINIT; }
extern "C" DSTATUS disk_status(BYTE) { return g_usb_host != nullptr && g_usb_host->storage_ready() ? 0 : STA_NOINIT; }
extern "C" DRESULT disk_read(BYTE, BYTE* buffer, DWORD sector, UINT count)
{
    return g_usb_host != nullptr && g_usb_host->read_blocks(static_cast<std::uint32_t>(sector), buffer, count) ? RES_OK : RES_NOTRDY;
}
extern "C" DRESULT disk_write(BYTE, const BYTE* buffer, DWORD sector, UINT count)
{
    return g_usb_host != nullptr && g_usb_host->write_blocks(static_cast<std::uint32_t>(sector), const_cast<BYTE*>(buffer), count) ? RES_OK : RES_NOTRDY;
}
extern "C" DRESULT disk_ioctl(BYTE, BYTE, void*) { return RES_OK; }

namespace brick::platform::stm32::f1
{

std::size_t Stm32UsbFile::read(void* buffer, std::size_t size, std::size_t count)
{
#if !BRICK_ST280_ENABLE_USB_READ
    (void)buffer; (void)size; (void)count;
    return 0;
#else
    UINT bytes = 0;
    return f_read(&file_, buffer, static_cast<UINT>(size * count), &bytes) == FR_OK ? bytes / size : 0;
#endif
}

std::size_t Stm32UsbFile::write(const void* buffer, std::size_t size, std::size_t count)
{
#if !BRICK_ST280_ENABLE_USB_WRITE
    (void)buffer; (void)size; (void)count;
    return 0;
#else
    UINT bytes = 0;
    return f_write(&file_, buffer, static_cast<UINT>(size * count), &bytes) == FR_OK ? bytes / size : 0;
#endif
}

bool Stm32UsbFile::seek(long offset, int origin)
{
    DWORD target = 0;
    if (origin == 0) target = static_cast<DWORD>(offset);
    else if (origin == 1) target = f_tell(&file_) + static_cast<DWORD>(offset);
    else if (origin == 2) target = f_size(&file_) + static_cast<DWORD>(offset);
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

void Stm32UsbFileSystem::unmount()
{
    if (!mounted_)
        return;
    f_mount(nullptr, "", 0);
    mounted_ = false;
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
    if (std::strchr(mode, 'w') != nullptr) {
#if BRICK_ST280_ENABLE_USB_WRITE
        flags = FA_WRITE | FA_CREATE_ALWAYS;
#else
        return nullptr;
#endif
    }
    else if (std::strchr(mode, 'a') != nullptr) {
#if BRICK_ST280_ENABLE_USB_WRITE
        flags = FA_WRITE | FA_OPEN_ALWAYS;
#else
        return nullptr;
#endif
    }
#if !BRICK_ST280_ENABLE_USB_READ
    if (std::strchr(mode, 'r') != nullptr)
        return nullptr;
#endif
    FIL file{};
    if (f_open(&file, path, flags) != FR_OK) return nullptr;
    if (std::strchr(mode, 'a') != nullptr && f_lseek(&file, f_size(&file)) != FR_OK) { f_close(&file); return nullptr; }
    return std::make_unique<Stm32UsbFile>(file);
}

}  // namespace brick::platform::stm32::f1
