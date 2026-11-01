#include "brick/platform/esp32/File.h"

#include <cerrno>

namespace brick::platform::esp32
{

File::~File()
{
    close();
}

Status File::open(const char* path, const char* mode)
{
    close();
    handle_ = std::fopen(path, mode);
    if (handle_ != nullptr)
        return Status::ok;
    return errno == ENOENT ? Status::not_found : Status::error;
}

void File::close()
{
    if (handle_ != nullptr)
        std::fclose(handle_);
    handle_ = nullptr;
}

std::size_t File::read(void* buffer, std::size_t size, std::size_t count)
{
    return handle_ == nullptr ? 0 : std::fread(buffer, size, count, handle_);
}

std::size_t File::write(const void* buffer, std::size_t size, std::size_t count)
{
    return handle_ == nullptr ? 0 : std::fwrite(buffer, size, count, handle_);
}

Status File::seek(long offset, int origin)
{
    if (handle_ == nullptr)
        return Status::not_ready;
    return std::fseek(handle_, offset, origin) == 0 ? Status::ok : Status::invalid_argument;
}

}  // namespace brick::platform::esp32
