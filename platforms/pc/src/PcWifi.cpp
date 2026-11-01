#include "brick/platform/pc/PcWifi.h"

#include <cstring>

#include "brick/core/network/WifiConfigCheck.h"
#include "brick/core/network/WifiScanList.h"

namespace brick::platform::pc
{

using brick::interfaces::network::IpInfo;
using brick::interfaces::network::LinkState;
using brick::interfaces::network::MacAddress;
using brick::interfaces::network::WifiFailure;
using brick::interfaces::network::WifiNetwork;

namespace
{
    constexpr IpInfo kStationAddress{
        { { 192, 168, 1, 50 } }, { { 255, 255, 255, 0 } }, { { 192, 168, 1, 1 } }, { { 192, 168, 1, 1 } }
    };
    constexpr IpInfo kHotspotAddress{ { { 192, 168, 4, 1 } }, { { 255, 255, 255, 0 } }, { { 192, 168, 4, 1 } }, {} };
    // Locally administered addresses, like a virtual adapter.
    constexpr MacAddress kStationMac{ { 0x02, 0x42, 0xB1, 0xC0, 0x00, 0x01 } };
    constexpr MacAddress kHotspotMac{ { 0x02, 0x42, 0xB1, 0xC0, 0x00, 0x02 } };
}  // namespace

std::uint32_t PcWifiStation::elapsed_ms_(std::uint32_t since_ms) const
{
    return time_.millis() - since_ms;
}

std::uint32_t PcWifiStation::outcome_delay_ms_() const
{
    return outcome_ == WifiFailure::network_not_found ? kNotFoundMs : kJoinMs;
}

LinkState PcWifiStation::link_state() const
{
    if (!joining_)
        return LinkState::down;
    if (elapsed_ms_(join_start_ms_) < outcome_delay_ms_())
        return LinkState::connecting;
    return outcome_ == WifiFailure::none ? LinkState::up : LinkState::down;
}

WifiFailure PcWifiStation::failure() const
{
    return joining_ && elapsed_ms_(join_start_ms_) >= outcome_delay_ms_() ? outcome_ : WifiFailure::none;
}

Status PcWifiStation::ip_info(IpInfo& info) const
{
    if (link_state() != LinkState::up)
        return Status::not_ready;
    info = kStationAddress;
    return Status::ok;
}

Status PcWifiStation::mac_address(MacAddress& mac) const
{
    mac = kStationMac;
    return Status::ok;
}

Status PcWifiStation::set_default_route()
{
    return link_state() == LinkState::up ? Status::ok : Status::not_ready;
}

Status PcWifiStation::start_scan()
{
    if (scanning_ && elapsed_ms_(scan_start_ms_) < kScanMs)
        return Status::busy;
    scanning_      = true;
    scan_start_ms_ = time_.millis();
    return Status::ok;
}

Status PcWifiStation::scan_results(WifiNetwork* networks, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (!scanning_)
        return Status::not_ready;
    if (elapsed_ms_(scan_start_ms_) < kScanMs)
        return Status::busy;
    if (networks == nullptr && capacity > 0)
        return Status::invalid_argument;
    for (std::size_t index = 0; index < networks_.size(); ++index)
        brick::core::network::add_scanned_network(networks, capacity, count, to_network_(index));
    return Status::ok;
}

Status PcWifiStation::connect(const brick::interfaces::network::WifiCredentials& credentials)
{
    if (const Status status = brick::core::network::check_credentials(credentials); !is_ok(status))
        return status;
    joining_       = true;
    join_start_ms_ = time_.millis();
    outcome_       = WifiFailure::network_not_found;
    for (std::size_t index = 0; index < networks_.size(); ++index)
    {
        if (networks_[index].ssid != credentials.ssid)
            continue;
        target_                    = index;
        const std::string password = credentials.password == nullptr ? "" : credentials.password;
        outcome_ = password == networks_[index].password ? WifiFailure::none : WifiFailure::authentication_failed;
        break;
    }
    return Status::ok;
}

Status PcWifiStation::disconnect()
{
    joining_ = false;
    return Status::ok;
}

Status PcWifiStation::connected_network(WifiNetwork& network) const
{
    if (link_state() != LinkState::up)
        return Status::not_ready;
    network = to_network_(target_);
    return Status::ok;
}

WifiNetwork PcWifiStation::to_network_(std::size_t index) const
{
    const PcWifiNetwork& source = networks_[index];
    WifiNetwork          network;
    std::strncpy(network.ssid, source.ssid.c_str(), sizeof(network.ssid) - 1);
    network.bssid    = { { 0x02, 0x00, 0x5E, 0x00, 0x00, static_cast<std::uint8_t>(index + 1) } };
    network.rssi_dbm = source.rssi_dbm;
    network.channel  = source.channel;
    network.security = source.password.empty() ? brick::interfaces::network::WifiSecurity::open :
                                                 brick::interfaces::network::WifiSecurity::wpa2;
    return network;
}

LinkState PcWifiAccessPoint::link_state() const
{
    return running_ ? LinkState::up : LinkState::down;
}

Status PcWifiAccessPoint::ip_info(IpInfo& info) const
{
    if (!running_)
        return Status::not_ready;
    info = kHotspotAddress;
    return Status::ok;
}

Status PcWifiAccessPoint::mac_address(MacAddress& mac) const
{
    mac = kHotspotMac;
    return Status::ok;
}

Status PcWifiAccessPoint::set_default_route()
{
    return running_ ? Status::ok : Status::not_ready;
}

Status PcWifiAccessPoint::start(const brick::interfaces::network::WifiAccessPointConfig& config)
{
    if (const Status status = brick::core::network::check_access_point(config); !is_ok(status))
        return status;
    ssid_    = config.ssid;
    running_ = true;
    return Status::ok;
}

Status PcWifiAccessPoint::stop()
{
    ssid_.clear();
    running_ = false;
    return Status::ok;
}

PcWifi::PcWifi(PcTime& time, std::vector<PcWifiNetwork> networks)
    : networks_(networks.empty() ? default_networks() : std::move(networks)), station_(time, networks_)
{
}

std::vector<PcWifiNetwork> PcWifi::default_networks()
{
    return { { "BRICK-LAB", "brick-lab-pass", -48, 6 },
             { "Neighbour", "neighbour-pass", -67, 1 },
             { "Cafe Free", "", -79, 11 } };
}

}  // namespace brick::platform::pc
