#pragma once

#include <cstddef>

#include "brick/interfaces/network/INetworkInterface.h"
#include "brick/interfaces/network/WifiTypes.h"

namespace brick::interfaces::network
{

/// Wi-Fi access point (hotspot) other devices join, for example to set the
/// device up. It can run alongside the station of the same radio; it then
/// follows the station's channel.
class IWifiAccessPoint : public INetworkInterface
{
public:
    /// Starts the hotspot, replacing a running one; link_state() becomes up
    /// once it is on the air. Status::invalid_argument for an empty or too
    /// long SSID or a password that is neither empty nor 8..63 characters.
    virtual Status start(const WifiAccessPointConfig& config) = 0;
    virtual Status stop()                                     = 0;
    /// Number of devices currently joined.
    virtual std::size_t client_count() const = 0;
};

}  // namespace brick::interfaces::network
