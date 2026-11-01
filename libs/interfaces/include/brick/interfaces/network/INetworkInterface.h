#pragma once

#include "brick/Status.h"
#include "brick/interfaces/network/NetworkTypes.h"

namespace brick::interfaces::network
{

/// A network connection traffic can go through: Wi-Fi station, Wi-Fi access
/// point, Ethernet or the PC's own network. Sockets (ITcpSocket,
/// IDnsResolver) use whichever interface is the default route, so code that
/// only needs "the internet" depends on this interface, not on the kind of
/// link.
class INetworkInterface
{
public:
    virtual ~INetworkInterface() = default;

    /// Constant name for logs and screens, e.g. "wifi" or "ethernet".
    virtual const char* name() const       = 0;
    virtual LinkState   link_state() const = 0;
    /// Addresses of the interface; Status::not_ready while it is not up.
    virtual Status ip_info(IpInfo& info) const = 0;
    /// Status::not_ready before the interface was started.
    virtual Status mac_address(MacAddress& mac) const = 0;
    /// Sends traffic for other networks (the internet) through this
    /// interface. Status::not_ready while it is not up.
    virtual Status set_default_route() = 0;
};

}  // namespace brick::interfaces::network
