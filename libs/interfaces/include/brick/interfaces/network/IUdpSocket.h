#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/network/NetworkTypes.h"

namespace brick::interfaces::network
{

/// UDP socket that never blocks, bound to a local port on every interface,
/// for a small server such as a DNS for a setup hotspot.
class IUdpSocket
{
public:
    virtual ~IUdpSocket() = default;

    /// Opens the socket on port, closing a previous one. Status::not_ready
    /// when the network is not started, Status::busy when the port is taken.
    virtual Status open(std::uint16_t port) = 0;
    /// Copies the next datagram that arrived (its first capacity bytes; the
    /// rest is dropped) and its sender. Status::no_data when none waits,
    /// Status::not_ready when the socket is not open.
    virtual Status receive_from(std::uint8_t* data, std::size_t capacity, std::size_t& received, Endpoint& from) = 0;
    /// Sends one datagram. Status::busy when no buffer is free now,
    /// Status::not_ready when the socket is not open.
    virtual Status send_to(const Endpoint& to, const std::uint8_t* data, std::size_t size) = 0;
    /// Closes the socket; safe to call when already closed.
    virtual void close() = 0;
};

}  // namespace brick::interfaces::network
