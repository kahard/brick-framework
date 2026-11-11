#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/network/ITcpSocket.h"

namespace brick::interfaces::network
{

/// TCP server port that never blocks: listen(), then poll accept() for the
/// connections that came in. It listens on every interface (station,
/// hotspot, Ethernet). An accepted connection is an ITcpSocket the listener
/// owns (it has room for a few); closing it gives its room back.
class ITcpListener
{
public:
    virtual ~ITcpListener() = default;

    /// Starts listening on port, replacing a previous port (accepted
    /// connections stay open). Status::not_ready when the network is not
    /// started, Status::busy when the port is taken.
    virtual Status listen(std::uint16_t port) = 0;
    /// The next connection that came in, already connected, or nullptr when
    /// none waits or nothing listens. The caller sends and receives, then
    /// closes it. Its ITcpSocket::connect() is not used.
    virtual ITcpSocket* accept() = 0;
    /// Stops listening and closes every accepted connection.
    virtual void close() = 0;
};

}  // namespace brick::interfaces::network
