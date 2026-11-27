#include "brick/platform/stm32/h7/Stm32MemoryProvider.h"

#include <initializer_list>

namespace brick::platform::stm32::h7
{

namespace
{
    constexpr std::size_t kMinAlignment = 32;  ///< a cache line, so cache maintenance never touches a neighbour
}  // namespace

Stm32MemoryProvider::Stm32MemoryProvider(Stm32MemoryProviderConfig config)
    : on_chip_{ config.on_chip, config.on_chip.begin, 0 }, external_{ config.external, config.external.begin, 0 }
{
}

void* Stm32MemoryProvider::allocate(std::size_t bytes, brick::interfaces::memory::MemoryKind kind,
                                    std::size_t alignment)
{
    using brick::interfaces::memory::MemoryKind;
    if (bytes == 0)
        return nullptr;
    if (kind == MemoryKind::external)
        return take_(external_, bytes, alignment);
    if (void* block = take_(on_chip_, bytes, alignment); block != nullptr)
        return block;
    return take_(external_, bytes, alignment);
}

void Stm32MemoryProvider::release(void* block)
{
    const auto address = reinterpret_cast<std::uintptr_t>(block);
    for (Cursor* cursor : { &on_chip_, &external_ })
    {
        if (address != 0 && address == cursor->last)
        {
            cursor->next = cursor->last;
            cursor->last = 0;
        }
    }
}

void* Stm32MemoryProvider::take_(Cursor& cursor, std::size_t bytes, std::size_t alignment)
{
    const std::size_t    align = alignment > kMinAlignment ? alignment : kMinAlignment;
    const std::uintptr_t start = (cursor.next + align - 1) & ~static_cast<std::uintptr_t>(align - 1);
    if (cursor.region.begin == 0 || start < cursor.next || start > cursor.region.end
        || bytes > cursor.region.end - start)
        return nullptr;
    cursor.last = start;
    cursor.next = start + bytes;
    return reinterpret_cast<void*>(start);
}

}  // namespace brick::platform::stm32::h7
