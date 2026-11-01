#include "brick/platform/esp8266/network/Esp8266Wifi.h"

#include <ESP8266WiFi.h>

#include "brick/core/network/WifiConfigCheck.h"

namespace brick::platform::esp8266
{

using brick::interfaces::network::IpInfo;
using brick::interfaces::network::Ipv4Address;
using brick::interfaces::network::LinkState;

namespace
{
    constexpr Ipv4Address kHotspotNetmask{ { 255, 255, 255, 0 } };

    Ipv4Address to_ipv4(const IPAddress& address)
    {
        return { { address[0], address[1], address[2], address[3] } };
    }
}  // namespace

Status Esp8266Wifi::begin()
{
    if (started_)
        return Status::ok;
    // The application owns the credentials and the retries.
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA))
        return Status::error;
    WiFi.setAutoReconnect(false);
    if (config_.hostname != nullptr)
        WiFi.hostname(config_.hostname);
    WiFi.setSleepMode(config_.power_save ? WIFI_MODEM_SLEEP : WIFI_NONE_SLEEP);
    started_ = true;
    return Status::ok;
}

LinkState Esp8266WifiAccessPoint::link_state() const
{
    return running_ ? LinkState::up : LinkState::down;
}

Status Esp8266WifiAccessPoint::ip_info(IpInfo& info) const
{
    if (!running_)
        return Status::not_ready;
    info.address = to_ipv4(WiFi.softAPIP());
    info.netmask = kHotspotNetmask;
    info.gateway = info.address;
    info.dns     = {};
    return Status::ok;
}

Status Esp8266WifiAccessPoint::mac_address(brick::interfaces::network::MacAddress& mac) const
{
    if (!radio_.started())
        return Status::not_ready;
    WiFi.softAPmacAddress(mac.octets.data());
    return Status::ok;
}

Status Esp8266WifiAccessPoint::set_default_route()
{
    return running_ ? Status::ok : Status::not_ready;
}

Status Esp8266WifiAccessPoint::start(const brick::interfaces::network::WifiAccessPointConfig& config)
{
    if (!radio_.started())
        return Status::not_ready;
    if (const Status status = brick::core::network::check_access_point(config); !is_ok(status))
        return status;
    if (!WiFi.mode(WIFI_AP_STA))
        return Status::error;
    const bool open = brick::core::network::bounded_length(config.password, 1) == 0;
    running_        = WiFi.softAP(config.ssid, open ? nullptr : config.password, config.channel, config.hidden ? 1 : 0,
                                  config.max_clients);
    return running_ ? Status::ok : Status::error;
}

Status Esp8266WifiAccessPoint::stop()
{
    if (!radio_.started())
        return Status::not_ready;
    running_ = false;
    // wifioff: switches the hotspot half off, the station stays.
    return WiFi.softAPdisconnect(true) ? Status::ok : Status::error;
}

std::size_t Esp8266WifiAccessPoint::client_count() const
{
    return running_ ? WiFi.softAPgetStationNum() : 0;
}

}  // namespace brick::platform::esp8266
