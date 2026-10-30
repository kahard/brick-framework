#include "brick/platform/esp32/File.h"

namespace brick::platform::esp32
{

File::~File()
{
    close();
}

bool File::open(const char* path, const char* mode)
{
    close();
    handle_ = std::fopen(path, mode);
    return handle_ != nullptr;
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

bool File::seek(long offset, int origin)
{
    return handle_ != nullptr && std::fseek(handle_, offset, origin) == 0;
}

}  // namespace brick::platform::esp32
