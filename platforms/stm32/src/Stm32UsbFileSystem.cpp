#include "brick/platform/stm32/f1/Stm32UsbFileSystem.h"

#include <cstring>

#include "diskio.h"

#ifndef BRICK_STM32_USB_FS_READ
#    define BRICK_STM32_USB_FS_READ 1
#endif
#ifndef BRICK_STM32_USB_FS_WRITE
#    define BRICK_STM32_USB_FS_WRITE 1
#endif

namespace
{
brick::platform::stm32::f1::Stm32UsbHost* g_usb_host = nullptr;
}

extern "C" DSTATUS disk_initialize(BYTE)
{
    return g_usb_host != nullptr && g_usb_host->storage_ready() ? 0 : STA_NOINIT;
}
extern "C" DSTATUS disk_status(BYTE)
{
    return g_usb_host != nullptr && g_usb_host->storage_ready() ? 0 : STA_NOINIT;
}
extern "C" DRESULT disk_read(BYTE, BYTE* buffer, DWORD sector, UINT count)
{
    return g_usb_host != nullptr && g_usb_host->read_blocks(static_cast<std::uint32_t>(sector), buffer, count) ?
               RES_OK :
               RES_NOTRDY;
}
extern "C" DRESULT disk_write(BYTE, const BYTE* buffer, DWORD sector, UINT count)
{
    return g_usb_host != nullptr
                   && g_usb_host->write_blocks(static_cast<std::uint32_t>(sector), const_cast<BYTE*>(buffer), count) ?
               RES_OK :
               RES_NOTRDY;
}
extern "C" DRESULT disk_ioctl(BYTE, BYTE, void*)
{
    return RES_OK;
}

namespace brick::platform::stm32::f1
{

bool Stm32UsbFile::open(const char* path, const char* mode)
{
    close();
    BYTE flags = FA_READ;
    if (std::strchr(mode, 'w') != nullptr)
    {
#if BRICK_STM32_USB_FS_WRITE
        flags = FA_WRITE | FA_CREATE_ALWAYS;
#else
        return false;
#endif
    }
    else if (std::strchr(mode, 'a') != nullptr)
    {
#if BRICK_STM32_USB_FS_WRITE
        flags = FA_WRITE | FA_OPEN_ALWAYS;
#else
        return false;
#endif
    }
#if !BRICK_STM32_USB_FS_READ
    if (std::strchr(mode, 'r') != nullptr)
        return false;
#endif
    if (f_open(&file_, path, flags) != FR_OK)
        return false;
    if (std::strchr(mode, 'a') != nullptr && f_lseek(&file_, f_size(&file_)) != FR_OK)
    {
        f_close(&file_);
        return false;
    }
    open_ = true;
    return true;
}

void Stm32UsbFile::close()
{
    if (open_)
        f_close(&file_);
    open_ = false;
}

std::size_t Stm32UsbFile::read(void* buffer, std::size_t size, std::size_t count)
{
#if !BRICK_STM32_USB_FS_READ
    (void)buffer;
    (void)size;
    (void)count;
    return 0;
#else
    UINT bytes = 0;
    return f_read(&file_, buffer, static_cast<UINT>(size * count), &bytes) == FR_OK ? bytes / size : 0;
#endif
}

std::size_t Stm32UsbFile::write(const void* buffer, std::size_t size, std::size_t count)
{
#if !BRICK_STM32_USB_FS_WRITE
    (void)buffer;
    (void)size;
    (void)count;
    return 0;
#else
    UINT bytes = 0;
    return f_write(&file_, buffer, static_cast<UINT>(size * count), &bytes) == FR_OK ? bytes / size : 0;
#endif
}

bool Stm32UsbFile::seek(long offset, int origin)
{
    DWORD target = 0;
    if (origin == 0)
        target = static_cast<DWORD>(offset);
    else if (origin == 1)
        target = f_tell(&file_) + static_cast<DWORD>(offset);
    else if (origin == 2)
        target = f_size(&file_) + static_cast<DWORD>(offset);
    else
        return false;
    return f_lseek(&file_, target) == FR_OK;
}

bool Stm32UsbFileSystem::mount()
{
    if (mounted_)
        return true;
    g_usb_host                = &host_;
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

std::size_t Stm32UsbFileSystem::list_files(const char* path, brick::interfaces::storage::FileEntry* entries,
                                           std::size_t capacity)
{
    if (!mounted_)
        return 0;
    DIR     dir{};
    FILINFO info{};
    if (f_opendir(&dir, path) != FR_OK)
        return 0;
    std::size_t count = 0;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
    {
        if (info.fattrib & AM_DIR)
            continue;
        if (entries != nullptr && count < capacity)
            entries[count].set_name(info.fname);
        ++count;
    }
    f_closedir(&dir);
    return count;
}

bool Stm32UsbFileSystem::open(const char* path, const char* mode, brick::interfaces::storage::FileHandle& file)
{
    file.close();
    if (!mounted_ || mode == nullptr)
        return false;
    for (Stm32UsbFile& candidate : files_)
    {
        if (candidate.is_open())
            continue;
        if (!candidate.open(path, mode))
            return false;
        file.attach(*this, candidate);
        return true;
    }
    return false;
}

void Stm32UsbFileSystem::close(brick::interfaces::storage::IFile& file)
{
    static_cast<Stm32UsbFile&>(file).close();
}

}  // namespace brick::platform::stm32::f1
