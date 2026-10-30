#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/memory/IMemoryProvider.h"
#include "esp_heap_caps.h"

namespace brick::platform::esp32
{

/// IMemoryProvider on the ESP-IDF capability allocator. Each MemoryKind maps
/// to one set of capabilities; there is deliberately no fallback, so a
/// missing PSRAM shows up as a failed allocation instead of exhausted internal
/// RAM later on.
class EspMemory final : public brick::interfaces::memory::IMemoryProvider
{
public:
    void* allocate(std::size_t bytes, brick::interfaces::memory::MemoryKind kind, std::size_t alignment) override
    {
        if (bytes == 0)
            return nullptr;
        // brick-rules: allow(malloc) this is the allocator behind IMemoryProvider
        return heap_caps_aligned_alloc(alignment < 4 ? 4 : alignment, bytes, capabilities(kind));
    }

    // brick-rules: allow(malloc) this is the allocator behind IMemoryProvider
    void release(void* block) override { heap_caps_free(block); }

    static std::uint32_t capabilities(brick::interfaces::memory::MemoryKind kind)
    {
        switch (kind)
        {
            case brick::interfaces::memory::MemoryKind::internal:
                return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
            case brick::interfaces::memory::MemoryKind::dma:
                return MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
            case brick::interfaces::memory::MemoryKind::external:
                return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        }
        return MALLOC_CAP_8BIT;
    }
};

}  // namespace brick::platform::esp32
