#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/communication/ISerialPort.h"

namespace brick::platform::stm32::h7
{

/// A serial port over the debug probe (SEGGER RTT, up channel 0): write()
/// copies bytes into a ring buffer in RAM, which openocd ("rtt setup",
/// "rtt start") or J-Link reads through SWD while the program runs. Bytes
/// that do not fit are dropped, so a program without a debugger attached
/// never waits. read() returns nothing (no down channel).
///
/// The control block must be in RAM the debugger can read without the data
/// cache in the way: the DTCM, or memory that is not cached.
class RttSerialPort final : public brick::interfaces::communication::ISerialPort
{
public:
    static constexpr std::size_t kBufferBytes = 2048;

    Status      begin() override;
    std::size_t read(std::uint8_t* buffer, std::size_t capacity) override;
    std::size_t write(const std::uint8_t* data, std::size_t length) override;

private:
    /// SEGGER's ring buffer layout; RdOff is written by the debugger.
    struct RingBuffer
    {
        const char*                name   = nullptr;
        char*                      buffer = nullptr;
        std::uint32_t              size   = 0;
        std::atomic<std::uint32_t> write_offset{ 0 };
        std::atomic<std::uint32_t> read_offset{ 0 };
        std::uint32_t              flags = 0;
    };

    /// SEGGER's control block: the debugger finds it by the ID at its start.
    struct ControlBlock
    {
        char         id[16]{};
        std::int32_t up_buffers   = 1;
        std::int32_t down_buffers = 0;
        RingBuffer   up;
    };

    ControlBlock control_;
    char         buffer_[kBufferBytes]{};
    bool         started_ = false;
};

}  // namespace brick::platform::stm32::h7
