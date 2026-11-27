#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/memory/IMemoryProvider.h"

namespace brick::platform::stm32::h7
{

/// A range of memory the provider hands out.
struct MemoryRegion
{
    std::uintptr_t begin = 0;
    std::uintptr_t end   = 0;
};

struct Stm32MemoryProviderConfig
{
    /// On-chip RAM that DMA engines reach too (AXI SRAM): internal and dma.
    MemoryRegion on_chip;
    /// External RAM (SDRAM): external, and internal or dma when the on-chip
    /// region is full.
    MemoryRegion external;
};

/// Blocks cut one after another from fixed regions, as the embedded rules
/// ask: requested once at start and kept. release() takes back only the
/// last block of a region; any other block stays used.
class Stm32MemoryProvider final : public brick::interfaces::memory::IMemoryProvider
{
public:
    explicit Stm32MemoryProvider(Stm32MemoryProviderConfig config);

    void* allocate(std::size_t bytes, brick::interfaces::memory::MemoryKind kind, std::size_t alignment) override;
    void  release(void* block) override;

private:
    struct Cursor
    {
        MemoryRegion   region;
        std::uintptr_t next = 0;
        std::uintptr_t last = 0;  ///< start of the last block, 0 when none
    };

    static void* take_(Cursor& cursor, std::size_t bytes, std::size_t alignment);

    Cursor on_chip_;
    Cursor external_;
};

}  // namespace brick::platform::stm32::h7
