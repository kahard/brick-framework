#pragma once

#include "brick/Status.h"
#include "brick/interfaces/network/NetworkTypes.h"

namespace brick::interfaces::network
{

/// Looks up the IPv4 address of a host name in the background: resolve(),
/// then poll result() from the loop. One lookup at a time.
class IDnsResolver
{
public:
    virtual ~IDnsResolver() = default;

    /// Starts looking up host_name, cancelling a lookup still running. A
    /// dotted address ("192.168.1.10") resolves at once.
    /// Status::invalid_argument for an empty or too long name.
    virtual Status resolve(const char* host_name) = 0;
    /// Status::busy while the lookup runs; Status::ok with the address;
    /// Status::not_found when the name does not exist; Status::timeout when
    /// no server answered; Status::not_ready when no lookup was started.
    virtual Status result(Ipv4Address& address) = 0;
    /// Forgets the running lookup; result() returns Status::not_ready.
    virtual void cancel() = 0;
};

}  // namespace brick::interfaces::network
