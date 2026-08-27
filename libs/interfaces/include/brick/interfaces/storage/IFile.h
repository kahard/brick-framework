#pragma once

#include <cstddef>
namespace brick::interfaces::storage
{

class IFile
{
public:
    virtual ~IFile()                                                            = default;
    virtual std::size_t read(void* buffer, std::size_t size, std::size_t count) = 0;
    // Returns the number of complete items written. The default keeps
    // read-only filesystem implementations source-compatible.
    virtual std::size_t write(const void* buffer, std::size_t size, std::size_t count)
    {
        (void)buffer;
        (void)size;
        (void)count;
        return 0;
    }
    virtual bool        seek(long offset, int origin)                           = 0;
};

}  // namespace brick::interfaces::storage
