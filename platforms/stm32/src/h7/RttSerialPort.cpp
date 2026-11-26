#include "brick/platform/stm32/h7/RttSerialPort.h"

#include <algorithm>
#include <cstring>

#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

namespace
{
    constexpr char          kId[]            = "SEGGER RTT";
    constexpr char          kChannelName[]   = "Terminal";
    constexpr std::uint32_t kModeNoBlockSkip = 0;  ///< drop what does not fit
}  // namespace

/// Fills the control block and writes the ID last, so a debugger searching
/// memory never finds a half-made block.
Status RttSerialPort::begin()
{
    if (started_)
        return Status::ok;
    control_.up_buffers   = 1;
    control_.down_buffers = 0;
    control_.up.name      = kChannelName;
    control_.up.buffer    = buffer_;
    control_.up.size      = kBufferBytes;
    control_.up.write_offset.store(0, std::memory_order_relaxed);
    control_.up.read_offset.store(0, std::memory_order_relaxed);
    control_.up.flags = kModeNoBlockSkip;
    __DSB();
    std::memcpy(control_.id, kId, sizeof(kId));
    __DSB();
    started_ = true;
    return Status::ok;
}

std::size_t RttSerialPort::read(std::uint8_t* buffer, std::size_t capacity)
{
    (void)buffer;
    (void)capacity;
    return 0;
}

/// Copies as much as fits between the write offset and the debugger's read
/// offset (one byte stays free to tell a full buffer from an empty one).
std::size_t RttSerialPort::write(const std::uint8_t* data, std::size_t length)
{
    if (!started_ || data == nullptr)
        return 0;
    RingBuffer&         ring  = control_.up;
    const std::uint32_t read  = ring.read_offset.load(std::memory_order_acquire);
    std::uint32_t       write = ring.write_offset.load(std::memory_order_relaxed);
    const std::uint32_t free  = read > write ? read - write - 1 : ring.size - write + read - 1;
    std::size_t         left  = std::min<std::size_t>(length, free);
    const std::size_t   taken = left;
    while (left != 0)
    {
        const std::size_t chunk = std::min<std::size_t>(left, ring.size - write);
        std::memcpy(ring.buffer + write, data, chunk);
        data += chunk;
        left -= chunk;
        write = static_cast<std::uint32_t>((write + chunk) % ring.size);
    }
    ring.write_offset.store(write, std::memory_order_release);
    return taken;
}

}  // namespace brick::platform::stm32::h7
