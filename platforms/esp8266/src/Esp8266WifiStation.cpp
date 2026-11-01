#include <ESP8266WiFi.h>
#include <cstring>

#include "brick/core/network/WifiConfigCheck.h"
#include "brick/core/network/WifiScanList.h"
#include "brick/platform/esp8266/network/Esp8266Wifi.h"

namespace brick::platform::esp8266
{

using brick::interfaces::network::IpInfo;
using brick::interfaces::network::Ipv4Address;
using brick::interfaces::network::LinkState;
using brick::interfaces::network::WifiFailure;
using brick::interfaces::network::WifiNetwork;
using brick::interfaces::network::WifiSecurity;

namespace
{
    Ipv4Address to_ipv4(const IPAddress& address)
    {
        return { { address[0], address[1], address[2], address[3] } };
    }

    WifiSecurity to_security(AUTH_MODE mode)
    {
        switch (mode)
        {
            case AUTH_OPEN:
                return WifiSecurity::open;
            case AUTH_WEP:
                return WifiSecurity::wep;
            case AUTH_WPA_PSK:
                return WifiSecurity::wpa;
            case AUTH_WPA2_PSK:
            case AUTH_WPA_WPA2_PSK:
                return WifiSecurity::wpa2;
            default:
                return WifiSecurity::unknown;
        }
    }

    WifiNetwork to_network(const bss_info& info)
    {
        WifiNetwork       network;
        const std::size_t length = info.ssid_len < sizeof(info.ssid) ? info.ssid_len : sizeof(info.ssid);
        std::memcpy(network.ssid, info.ssid, length);
        network.ssid[length] = '\0';
        std::memcpy(network.bssid.octets.data(), info.bssid, network.bssid.octets.size());
        network.rssi_dbm = info.rssi;
        network.channel  = info.channel;
        network.security = to_security(info.authmode);
        return network;
    }
}  // namespace

LinkState Esp8266WifiStation::link_state() const
{
    if (!joining_)
        return LinkState::down;
    if (WiFi.status() == WL_CONNECTED)
    {
        was_up_ = true;
        return LinkState::up;
    }
    return failure() == WifiFailure::none ? LinkState::connecting : LinkState::down;
}

WifiFailure Esp8266WifiStation::failure() const
{
    if (!joining_)
        return WifiFailure::none;
    switch (WiFi.status())
    {
        case WL_CONNECTED:
            return WifiFailure::none;
        case WL_NO_SSID_AVAIL:
            return WifiFailure::network_not_found;
        case WL_WRONG_PASSWORD:
            return WifiFailure::authentication_failed;
        case WL_CONNECT_FAILED:
            return WifiFailure::other;
        default:
            return was_up_ ? WifiFailure::connection_lost : WifiFailure::none;
    }
}

Status Esp8266WifiStation::ip_info(IpInfo& info) const
{
    if (link_state() != LinkState::up)
        return Status::not_ready;
    info.address = to_ipv4(WiFi.localIP());
    info.netmask = to_ipv4(WiFi.subnetMask());
    info.gateway = to_ipv4(WiFi.gatewayIP());
    info.dns     = to_ipv4(WiFi.dnsIP(0));
    return Status::ok;
}

Status Esp8266WifiStation::mac_address(brick::interfaces::network::MacAddress& mac) const
{
    if (!radio_.started())
        return Status::not_ready;
    WiFi.macAddress(mac.octets.data());
    return Status::ok;
}

Status Esp8266WifiStation::set_default_route()
{
    // lwIP of the ESP8266 routes through the station whenever it is up.
    return link_state() == LinkState::up ? Status::ok : Status::not_ready;
}

Status Esp8266WifiStation::start_scan()
{
    if (!radio_.started())
        return Status::not_ready;
    if (scan_ == ScanState::running)
        return Status::busy;
    // Asynchronous, hidden networks included.
    if (WiFi.scanNetworks(true, true) != WIFI_SCAN_RUNNING)
        return Status::busy;
    scan_       = ScanState::running;
    scan_count_ = 0;
    return Status::ok;
}

Status Esp8266WifiStation::scan_results(WifiNetwork* networks, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (scan_ == ScanState::running)
        if (const Status status = collect_scan_(); !is_ok(status))
            return status;
    if (scan_ == ScanState::idle)
        return Status::not_ready;
    if (scan_ == ScanState::failed)
        return Status::error;
    if (networks == nullptr && capacity > 0)
        return Status::invalid_argument;
    for (; count < capacity && count < scan_count_; ++count)
        networks[count] = scan_results_[count];
    return Status::ok;
}

Status Esp8266WifiStation::collect_scan_()
{
    const int8_t found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING)
        return Status::busy;
    if (found < 0)
    {
        scan_ = ScanState::failed;
        return Status::error;
    }
    for (int index = 0; index < found; ++index)
        if (const bss_info* info = WiFi.getScanInfoByIndex(index); info != nullptr)
            brick::core::network::add_scanned_network(scan_results_.data(), scan_results_.size(), scan_count_,
                                                      to_network(*info));
    WiFi.scanDelete();
    scan_ = ScanState::done;
    return Status::ok;
}

Status Esp8266WifiStation::connect(const brick::interfaces::network::WifiCredentials& credentials)
{
    if (!radio_.started())
        return Status::not_ready;
    if (const Status status = brick::core::network::check_credentials(credentials); !is_ok(status))
        return status;
    const bool open = brick::core::network::bounded_length(credentials.password, 1) == 0;
    joining_        = true;
    was_up_         = false;
    // Non-blocking: progress shows in WiFi.status().
    return WiFi.begin(credentials.ssid, open ? nullptr : credentials.password) == WL_CONNECT_FAILED ? Status::error :
                                                                                                      Status::ok;
}

Status Esp8266WifiStation::disconnect()
{
    if (!radio_.started())
        return Status::not_ready;
    joining_ = false;
    was_up_  = false;
    WiFi.disconnect(false);
    return Status::ok;
}

Status Esp8266WifiStation::connected_network(WifiNetwork& network) const
{
    if (link_state() != LinkState::up)
        return Status::not_ready;
    station_config config{};
    if (!wifi_station_get_config(&config))
        return Status::error;
    network = {};
    std::memcpy(network.ssid, config.ssid, sizeof(network.ssid) - 1);
    if (const std::uint8_t* bssid = WiFi.BSSID(); bssid != nullptr)
        std::memcpy(network.bssid.octets.data(), bssid, network.bssid.octets.size());
    network.rssi_dbm = WiFi.RSSI();
    network.channel  = static_cast<std::uint8_t>(WiFi.channel());
    return Status::ok;
}

}  // namespace brick::platform::esp8266
