#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/memory/IMemoryProvider.h"

namespace brick::core::memory
{

/// Owns one block from an IMemoryProvider and releases it on destruction.
/// Allocate during initialization (embedded coding rule M1); a failed
/// allocation is reported, never replaced by a different kind of memory.
class MemoryBlock final
{
public:
    explicit MemoryBlock(brick::interfaces::memory::IMemoryProvider& provider) : provider_(provider) {}
    ~MemoryBlock() { release(); }
    MemoryBlock(const MemoryBlock&)            = delete;
    MemoryBlock& operator=(const MemoryBlock&) = delete;

    /// Status::invalid_argument when a block is already held or bytes is 0,
    /// Status::no_memory when the provider refuses.
    Status allocate(std::size_t bytes, brick::interfaces::memory::MemoryKind kind, std::size_t alignment = 64)
    {
        if (data_ != nullptr || bytes == 0)
            return Status::invalid_argument;
        data_ = static_cast<std::uint8_t*>(provider_.allocate(bytes, kind, alignment));
        size_ = data_ != nullptr ? bytes : 0;
        return data_ != nullptr ? Status::ok : Status::no_memory;
    }

    void release()
    {
        if (data_ != nullptr)
            provider_.release(data_);
        data_ = nullptr;
        size_ = 0;
    }

    std::uint8_t* data() const { return data_; }
    std::size_t   size() const { return size_; }
    explicit      operator bool() const { return data_ != nullptr; }

private:
    brick::interfaces::memory::IMemoryProvider& provider_;
    std::uint8_t*                               data_ = nullptr;
    std::size_t                                 size_ = 0;
};

}  // namespace brick::core::memory
