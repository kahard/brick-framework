#include "brick/platform/esp32/wroom32/CydSdSpiFileSystem.h"

namespace brick::platform::esp32::wroom32
{

Status CydSdSpiFileSystem::File::open(SdSpiFileSystem& filesystem, CydSpi3PinMux& pin_mux, const char* path,
                                      const char* mode)
{
    pin_mux_ = &pin_mux;
    pin_mux.select_sd();
    return filesystem.open(path, mode, file_);
}

void CydSdSpiFileSystem::File::close()
{
    if (pin_mux_ != nullptr)
        pin_mux_->select_sd();
    file_.close();
}

std::size_t CydSdSpiFileSystem::File::read(void* buffer, std::size_t size, std::size_t count)
{
    if (!file_)
        return 0;
    pin_mux_->select_sd();
    return file_->read(buffer, size, count);
}

std::size_t CydSdSpiFileSystem::File::write(const void* buffer, std::size_t size, std::size_t count)
{
    if (!file_)
        return 0;
    pin_mux_->select_sd();
    return file_->write(buffer, size, count);
}

Status CydSdSpiFileSystem::File::seek(long offset, int origin)
{
    if (!file_)
        return Status::not_ready;
    pin_mux_->select_sd();
    return file_->seek(offset, origin);
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

Status CydSdSpiFileSystem::mount()
{
    pin_mux_.select_sd();
    return filesystem_.mount();
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
    pin_mux_.select_sd();
    return filesystem_.probe(path);
}

std::size_t CydSdSpiFileSystem::list_files(const char* path, brick::interfaces::storage::FileEntry* entries,
                                           std::size_t capacity)
{
    pin_mux_.select_sd();
    return filesystem_.list_files(path, entries, capacity);
}

Status CydSdSpiFileSystem::open(const char* path, const char* mode, brick::interfaces::storage::FileHandle& file)
{
    file.close();
    for (File& candidate : files_)
    {
        if (candidate.is_open())
            continue;
        if (const Status status = candidate.open(filesystem_, pin_mux_, path, mode); !is_ok(status))
            return status;
        file.attach(*this, candidate);
        return Status::ok;
    }
    return Status::busy;
}

void CydSdSpiFileSystem::close(brick::interfaces::storage::IFile& file)
{
    static_cast<File&>(file).close();
}

}  // namespace brick::platform::esp32::wroom32
