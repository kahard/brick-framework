#pragma once

#include "brick/interfaces/storage/IFile.h"
#include <cstdio>

namespace brick::platform::esp32
{

class File final : public interfaces::storage::IFile
{
public:
    explicit File(std::FILE* handle);
    ~File() override;

    std::size_t read(void* buffer, std::size_t size, std::size_t count) override;
    bool        seek(long offset, int origin) override;

private:
    std::FILE* handle_;
};

}  // namespace brick::platform::esp32
