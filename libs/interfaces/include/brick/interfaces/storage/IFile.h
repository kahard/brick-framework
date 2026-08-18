#pragma once

#include <cstddef>
namespace brick::interfaces::storage
{

class IFile
{
public:
    virtual ~IFile()                                                            = default;
    virtual std::size_t read(void* buffer, std::size_t size, std::size_t count) = 0;
    virtual bool        seek(long offset, int origin)                           = 0;
};

}  // namespace brick::interfaces::storage
