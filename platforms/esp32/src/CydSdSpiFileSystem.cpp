#include "brick/platform/esp32/wroom32/CydSdSpiFileSystem.h"

namespace brick::platform::esp32::wroom32
{

bool CydSdSpiFileSystem::File::open(SdSpiFileSystem& filesystem, CydSpi3PinMux& pin_mux, const char* path,
                                    const char* mode)
{
    pin_mux_ = &pin_mux;
    return pin_mux.select_sd() && filesystem.open(path, mode, file_);
}

void CydSdSpiFileSystem::File::close()
{
    if (pin_mux_ != nullptr)
        pin_mux_->select_sd();
    file_.close();
}

std::size_t CydSdSpiFileSystem::File::read(void* buffer, std::size_t size, std::size_t count)
{
    return file_ && pin_mux_->select_sd() ? file_->read(buffer, size, count) : 0;
}

std::size_t CydSdSpiFileSystem::File::write(const void* buffer, std::size_t size, std::size_t count)
{
    return file_ && pin_mux_->select_sd() ? file_->write(buffer, size, count) : 0;
}

bool CydSdSpiFileSystem::File::seek(long offset, int origin)
{
    return file_ && pin_mux_->select_sd() && file_->seek(offset, origin);
}

CydSdSpiFileSystem::CydSdSpiFileSystem(SdSpiFileSystemConfig config, CydSpi3PinMux& pin_mux)
    : pin_mux_(pin_mux), filesystem_(config)
{
}

CydSdSpiFileSystem::~CydSdSpiFileSystem()
{
    for (File& file : files_)
        file.close();
    unmount();
}

bool CydSdSpiFileSystem::mount()
{
    return pin_mux_.select_sd() && filesystem_.mount();
}

void CydSdSpiFileSystem::unmount()
{
    pin_mux_.select_sd();
    filesystem_.unmount();
}

bool CydSdSpiFileSystem::mounted() const
{
    return filesystem_.mounted();
}

bool CydSdSpiFileSystem::probe(const char* path)
{
    return pin_mux_.select_sd() && filesystem_.probe(path);
}

std::size_t CydSdSpiFileSystem::list_files(const char* path, brick::interfaces::storage::FileEntry* entries,
                                           std::size_t capacity)
{
    if (!pin_mux_.select_sd())
        return 0;
    return filesystem_.list_files(path, entries, capacity);
}

bool CydSdSpiFileSystem::open(const char* path, const char* mode, brick::interfaces::storage::FileHandle& file)
{
    file.close();
    for (File& candidate : files_)
    {
        if (candidate.is_open())
            continue;
        if (!candidate.open(filesystem_, pin_mux_, path, mode))
            return false;
        file.attach(*this, candidate);
        return true;
    }
    return false;
}

void CydSdSpiFileSystem::close(brick::interfaces::storage::IFile& file)
{
    static_cast<File&>(file).close();
}

}  // namespace brick::platform::esp32::wroom32
