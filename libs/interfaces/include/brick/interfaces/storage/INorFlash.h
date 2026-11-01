#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"

namespace brick::interfaces::storage
{

/// Manufacturer and device ID of a serial NOR flash (JEDEC command 0x9F).
struct JedecId
{
    std::uint8_t manufacturer = 0;
    std::uint8_t memory_type  = 0;
    std::uint8_t capacity     = 0;

    /// False for 0x00 or 0xFF: no chip answered (data line stuck low or
    /// floating high).
    constexpr bool present() const { return manufacturer != 0x00 && manufacturer != 0xFF; }
};

/// A serial NOR flash chip. Writing needs erasing first and is not part of
/// this interface yet.
class INorFlash
{
public:
    virtual ~INorFlash()                                                     = default;
    virtual Status read_id(JedecId& id)                                      = 0;
    virtual Status read(std::uint32_t address, void* data, std::size_t size) = 0;
};

}  // namespace brick::interfaces::storage
