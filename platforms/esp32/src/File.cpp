#include "brick/platform/esp32/File.h"

namespace brick::platform::esp32
{

File::File(std::FILE* handle) : handle_(handle)
{
}

File::~File()
{
    if (handle_ != nullptr)
        std::fclose(handle_);
}

std::size_t File::read(void* buffer, std::size_t size, std::size_t count)
{
    return handle_ == nullptr ? 0 : std::fread(buffer, size, count, handle_);
}

std::size_t File::write(const void* buffer, std::size_t size, std::size_t count)
{
    return handle_ == nullptr ? 0 : std::fwrite(buffer, size, count, handle_);
}

bool File::seek(long offset, int origin)
{
    return handle_ != nullptr && std::fseek(handle_, offset, origin) == 0;
}

}  // namespace brick::platform::esp32
