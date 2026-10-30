#pragma once

#include <cstddef>
#include <cstdint>

namespace brick::interfaces::memory
{

/// Where a block must live. Platforms map these to their allocators, e.g.
/// ESP32 heap capabilities; a platform without the distinction treats them
/// all as ordinary memory.
enum class MemoryKind : std::uint8_t
{
    internal,  ///< fast on-chip RAM
    dma,       ///< reachable by DMA engines (display, SD)
    external,  ///< large external RAM (PSRAM)
};

/// Source of large or special-purpose memory blocks (frame buffers, DMA
/// stripes). Per the embedded coding rules, request blocks once during
/// initialization and keep them for the lifetime of the owner.
class IMemoryProvider
{
public:
    virtual ~IMemoryProvider() = default;

    /// Returns nullptr when the request cannot be satisfied.
    virtual void* allocate(std::size_t bytes, MemoryKind kind, std::size_t alignment) = 0;
    virtual void  release(void* block)                                                = 0;
};

}  // namespace brick::interfaces::memory
