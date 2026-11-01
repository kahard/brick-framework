#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/NetworkTypes.h"

namespace brick::interfaces::network
{

inline constexpr std::size_t kWifiSsidMaxLength     = 32;
inline constexpr std::size_t kWifiPasswordMinLength = 8;   ///< WPA passphrase
inline constexpr std::size_t kWifiPasswordMaxLength = 63;  ///< WPA passphrase

enum class WifiSecurity : std::uint8_t
{
    open,
    wep,
    wpa,
    wpa2,  ///< also WPA/WPA2 mixed mode
    wpa3,
    wpa2_wpa3,
    enterprise,
    unknown,
};

/// An access point found by a scan, or the one a station is joined to.
struct WifiNetwork
{
    char         ssid[kWifiSsidMaxLength + 1] = {};  ///< NUL-terminated, empty for a hidden network
    MacAddress   bssid;
    std::int8_t  rssi_dbm = 0;  ///< signal strength, e.g. -45 (strong) to -90 (weak)
    std::uint8_t channel  = 0;
    WifiSecurity security = WifiSecurity::unknown;
};

/// Network for a station to join. connect() copies the strings; they do not
/// need to outlive the call. A manager keeps a pointer to them instead.
struct WifiCredentials
{
    const char* ssid     = nullptr;
    const char* password = nullptr;  ///< nullptr or "" for an open network, otherwise 8..63 characters
    bool        hidden   = false;    ///< not broadcast: try it even when a scan does not show it
};

/// Hotspot started by IWifiAccessPoint::start(). The strings are copied.
struct WifiAccessPointConfig
{
    const char*  ssid        = nullptr;
    const char*  password    = nullptr;  ///< nullptr or "" for an open hotspot, otherwise 8..63 characters (WPA2)
    std::uint8_t channel     = 1;
    std::uint8_t max_clients = 4;
    bool         hidden      = false;
};

/// Why the last connection attempt failed or the connection was lost.
enum class WifiFailure : std::uint8_t
{
    none,
    network_not_found,      ///< no access point with this SSID answered
    authentication_failed,  ///< wrong password, or a security mode the access point rejects
    timeout,                ///< no connection or no DHCP address within the time limit
    connection_lost,        ///< the access point stopped answering or dropped the station
    other,
};

constexpr const char* to_string(WifiFailure failure)
{
    switch (failure)
    {
        case WifiFailure::none:
            return "none";
        case WifiFailure::network_not_found:
            return "network not found";
        case WifiFailure::authentication_failed:
            return "authentication failed";
        case WifiFailure::timeout:
            return "timeout";
        case WifiFailure::connection_lost:
            return "connection lost";
        case WifiFailure::other:
            return "other";
    }
    return "unknown";
}

constexpr const char* to_string(WifiSecurity security)
{
    switch (security)
    {
        case WifiSecurity::open:
            return "open";
        case WifiSecurity::wep:
            return "WEP";
        case WifiSecurity::wpa:
            return "WPA";
        case WifiSecurity::wpa2:
            return "WPA2";
        case WifiSecurity::wpa3:
            return "WPA3";
        case WifiSecurity::wpa2_wpa3:
            return "WPA2/WPA3";
        case WifiSecurity::enterprise:
            return "enterprise";
        case WifiSecurity::unknown:
            break;
    }
    return "unknown";
}

}  // namespace brick::interfaces::network
