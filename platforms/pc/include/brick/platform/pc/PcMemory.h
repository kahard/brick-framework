#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

#include "brick/interfaces/memory/IMemoryProvider.h"

namespace brick::platform::pc
{

/// Ordinary heap memory for every MemoryKind. Optionally limits the total so
/// that out-of-memory paths of an application can be exercised on the PC.
class PcMemory final : public brick::interfaces::memory::IMemoryProvider
{
public:
    explicit PcMemory(std::size_t limit_bytes = 0) : limit_(limit_bytes) {}

    void* allocate(std::size_t bytes, brick::interfaces::memory::MemoryKind kind, std::size_t alignment) override
    {
        (void)kind;
        if (bytes == 0 || alignment > kAlignment || (limit_ != 0 && used_ + bytes > limit_))
            return nullptr;
        void* block = ::operator new(bytes, std::align_val_t{ kAlignment }, std::nothrow);
        if (block != nullptr)
        {
            used_ += bytes;
            ++blocks_;
        }
        return block;
    }

    void release(void* block) override
    {
        // Sizes are not tracked per block; the limit is a coarse budget.
        if (block != nullptr && blocks_ > 0)
            --blocks_;
        ::operator delete(block, std::align_val_t{ kAlignment }, std::nothrow);
    }

    /// Every block is aligned to this; larger alignments are refused.
    static constexpr std::size_t kAlignment = 64;

    std::size_t used_bytes() const { return used_; }
    std::size_t live_blocks() const { return blocks_; }

private:
    std::size_t limit_;
    std::size_t used_   = 0;
    std::size_t blocks_ = 0;
};

}  // namespace brick::platform::pc
