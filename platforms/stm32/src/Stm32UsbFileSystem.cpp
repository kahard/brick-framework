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

DRESULT to_dresult(brick::Status status)
{
    switch (status)
    {
        case brick::Status::ok:
            return RES_OK;
        case brick::Status::not_ready:
            return RES_NOTRDY;
        default:
            return RES_ERROR;
    }
}
}  // namespace

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
    if (g_usb_host == nullptr)
        return RES_NOTRDY;
    return to_dresult(g_usb_host->read_blocks(static_cast<std::uint32_t>(sector), buffer, count));
}
extern "C" DRESULT disk_write(BYTE, const BYTE* buffer, DWORD sector, UINT count)
{
    if (g_usb_host == nullptr)
        return RES_NOTRDY;
    // USBH_MSC_Write takes a non-const buffer but does not modify it.
    return to_dresult(g_usb_host->write_blocks(static_cast<std::uint32_t>(sector), const_cast<BYTE*>(buffer), count));
}
extern "C" DRESULT disk_ioctl(BYTE, BYTE, void*)
{
    return RES_OK;
}

namespace brick::platform::stm32::f1
{

Status Stm32UsbFile::open(const char* path, const char* mode)
{
    close();
    BYTE flags = FA_READ;
    if (std::strchr(mode, 'w') != nullptr)
    {
#if BRICK_STM32_USB_FS_WRITE
        flags = FA_WRITE | FA_CREATE_ALWAYS;
#else
        return Status::not_supported;
#endif
    }
    else if (std::strchr(mode, 'a') != nullptr)
    {
#if BRICK_STM32_USB_FS_WRITE
        flags = FA_WRITE | FA_OPEN_ALWAYS;
#else
        return Status::not_supported;
#endif
    }
#if !BRICK_STM32_USB_FS_READ
    if (std::strchr(mode, 'r') != nullptr)
        return Status::not_supported;
#endif
    if (const FRESULT result = f_open(&file_, path, flags); result != FR_OK)
        return result == FR_NO_FILE || result == FR_NO_PATH ? Status::not_found : Status::error;
    if (std::strchr(mode, 'a') != nullptr && f_lseek(&file_, f_size(&file_)) != FR_OK)
    {
        f_close(&file_);
        return Status::error;
    }
    open_ = true;
    return Status::ok;
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

Status Stm32UsbFile::seek(long offset, int origin)
{
    DWORD target = 0;
    if (origin == 0)
        target = static_cast<DWORD>(offset);
    else if (origin == 1)
        target = f_tell(&file_) + static_cast<DWORD>(offset);
    else if (origin == 2)
        target = f_size(&file_) + static_cast<DWORD>(offset);
    else
        return Status::invalid_argument;
    return f_lseek(&file_, target) == FR_OK ? Status::ok : Status::error;
}

Status Stm32UsbFileSystem::mount()
{
    if (mounted_)
        return Status::ok;
    g_usb_host = &host_;
    if (!host_.storage_ready())
        return host_.port_connected() ? Status::busy : Status::not_ready;
    if (f_mount(&fatfs_, "", 1) != FR_OK)
        return Status::error;
    mounted_ = true;
    return Status::ok;
}

void Stm32UsbFileSystem::unmount()
{
    if (!mounted_)
        return;
    for (Stm32UsbFile& file : files_)
        file.close();
    f_mount(nullptr, "", 0);
    mounted_ = false;
}

bool Stm32UsbFileSystem::probe(const char* path)
{
    if (!mounted_ || path == nullptr || !host_.storage_ready())
        return false;
    FILINFO info{};
    if (f_stat(path, &info) == FR_OK)
        return true;
    // f_stat() does not accept the root directory.
    DIR dir{};
    if (f_opendir(&dir, path) != FR_OK)
        return false;
    f_closedir(&dir);
    return true;
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

Status Stm32UsbFileSystem::open(const char* path, const char* mode, brick::interfaces::storage::FileHandle& file)
{
    file.close();
    if (!mounted_)
        return Status::not_ready;
    if (mode == nullptr)
        return Status::invalid_argument;
    for (Stm32UsbFile& candidate : files_)
    {
        if (candidate.is_open())
            continue;
        if (const Status status = candidate.open(path, mode); !is_ok(status))
            return status;
        file.attach(*this, candidate);
        return Status::ok;
    }
    return Status::busy;
}

void Stm32UsbFileSystem::close(brick::interfaces::storage::IFile& file)
{
    static_cast<Stm32UsbFile&>(file).close();
}

}  // namespace brick::platform::stm32::f1
