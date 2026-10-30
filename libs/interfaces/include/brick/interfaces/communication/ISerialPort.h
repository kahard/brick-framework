#pragma once

#include <cstddef>
#include <cstdint>

namespace brick::interfaces::communication
{

/// Byte stream over a UART, USB CDC or a host pipe. Calls never block: read()
/// returns what has already arrived and write() what could be queued.
class ISerialPort
{
public:
    virtual ~ISerialPort() = default;

    virtual bool        begin()                                             = 0;
    virtual std::size_t read(std::uint8_t* buffer, std::size_t capacity)    = 0;
    virtual std::size_t write(const std::uint8_t* data, std::size_t length) = 0;
};

}  // namespace brick::interfaces::communication
