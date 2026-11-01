#pragma once

#include <cstddef>

#include "brick/interfaces/network/INetworkInterface.h"

namespace brick::core::network
{

/// The connection an application uses for "the internet": the first of
/// several interfaces, in priority order, that is up (e.g. Ethernet before
/// Wi-Fi). Whenever the choice changes it makes the new interface the
/// default route. It is an INetworkInterface itself, so an application
/// receives one network and does not care which link carries the traffic.
class NetworkSelector final : public interfaces::network::INetworkInterface
{
public:
    /// interfaces: highest priority first; the array must outlive the selector.
    NetworkSelector(interfaces::network::INetworkInterface* const* interfaces, std::size_t count)
        : interfaces_(interfaces), count_(interfaces == nullptr ? 0 : count)
    {
    }

    /// Picks the active interface; call from the loop. Returns the result of
    /// set_default_route() when the choice changed, otherwise Status::ok.
    Status update();
    /// The interface traffic goes through; nullptr while none is up.
    interfaces::network::INetworkInterface* active() const { return active_; }

    /// Name of the active interface, "none" while none is up.
    const char* name() const override;
    /// up with an active interface, connecting while any is connecting.
    interfaces::network::LinkState link_state() const override;
    Status                         ip_info(interfaces::network::IpInfo& info) const override;
    Status                         mac_address(interfaces::network::MacAddress& mac) const override;
    Status                         set_default_route() override;

private:
    interfaces::network::INetworkInterface* const* interfaces_;
    std::size_t                                    count_;
    interfaces::network::INetworkInterface*        active_ = nullptr;
};

}  // namespace brick::core::network
