#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"

namespace brick::interfaces::storage
{

class IByteStorage
{
public:
    virtual ~IByteStorage()                                                         = default;
    virtual Status read(std::uint32_t address, void* data, std::size_t size)        = 0;
    virtual Status write(std::uint32_t address, const void* data, std::size_t size) = 0;
};

}  // namespace brick::interfaces::storage
