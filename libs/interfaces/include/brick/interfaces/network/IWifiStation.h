#pragma once

#include <cstddef>

#include "brick/interfaces/network/INetworkInterface.h"
#include "brick/interfaces/network/WifiTypes.h"

namespace brick::interfaces::network
{

/// Wi-Fi client (station). Scanning and joining run in the background: start
/// them, then poll from the loop; nothing here blocks. A station does not
/// retry by itself; brick::core::network::WifiManager does.
class IWifiStation : public INetworkInterface
{
public:
    /// Starts looking for access points. Status::busy while a scan runs.
    virtual Status start_scan() = 0;
    /// Result of the last scan: Status::busy while it runs, Status::not_ready
    /// when none was started, Status::error when it failed. Copies the
    /// strongest networks first, at most capacity; count is the number copied.
    virtual Status scan_results(WifiNetwork* networks, std::size_t capacity, std::size_t& count) = 0;
    /// Starts joining a network, leaving the current one. Progress shows in
    /// link_state(): connecting, then up, or down with failure() set.
    /// Status::invalid_argument for an empty or too long SSID or password.
    virtual Status connect(const WifiCredentials& credentials) = 0;
    /// Leaves the network: link_state() becomes down, failure() none.
    virtual Status disconnect() = 0;
    /// Reason of the last failed attempt or lost connection; none while
    /// connected and after disconnect().
    virtual WifiFailure failure() const = 0;
    /// The access point the station is joined to, with the current signal;
    /// Status::not_ready when not joined.
    virtual Status connected_network(WifiNetwork& network) const = 0;
};

}  // namespace brick::interfaces::network
