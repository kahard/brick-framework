#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/network/NetworkTypes.h"

namespace brick::interfaces::network
{

/// TCP client connection that never blocks: connect(), poll
/// poll_connect() until it is no longer Status::busy, then send() and
/// receive() whatever is ready. The connection goes through the default
/// route (INetworkInterface::set_default_route()).
class ITcpSocket
{
public:
    virtual ~ITcpSocket() = default;

    /// Starts connecting, closing a previous connection first.
    /// Status::not_ready when the network is not started.
    virtual Status connect(const Endpoint& remote) = 0;
    /// Status::busy while the connection is being made; Status::ok once
    /// connected (and while it stays so); Status::timeout when the server did
    /// not answer; Status::error when it refused or the connection broke;
    /// Status::not_ready when connect() was not called.
    virtual Status poll_connect() = 0;
    /// Queues up to size bytes; sent is the number queued (may be less).
    /// Status::busy when nothing fits now; Status::not_ready when not
    /// connected; Status::error when the connection broke.
    virtual Status send(const std::uint8_t* data, std::size_t size, std::size_t& sent) = 0;
    /// Copies up to capacity received bytes; received is the number copied.
    /// Status::no_data when nothing has arrived yet; Status::not_ready when
    /// the connection is closed (by the server, once everything it sent was
    /// read, or never opened); Status::error when it broke.
    virtual Status receive(std::uint8_t* data, std::size_t capacity, std::size_t& received) = 0;
    /// Closes the connection; safe to call when already closed.
    virtual void close() = 0;
};

}  // namespace brick::interfaces::network
