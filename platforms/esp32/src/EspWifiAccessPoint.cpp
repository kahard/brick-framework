#include <cstring>

#include "brick/core/network/WifiConfigCheck.h"
#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/network/EspNetworkStack.h"
#include "brick/platform/esp32/network/EspWifi.h"
#include "esp_wifi.h"

namespace brick::platform::esp32
{

using interfaces::network::LinkState;

Status EspWifiAccessPoint::ip_info(interfaces::network::IpInfo& info) const
{
    return state_.load() == LinkState::up ? read_ip_info(netif_, info) : Status::not_ready;
}

Status EspWifiAccessPoint::mac_address(interfaces::network::MacAddress& mac) const
{
    return radio_.started() ? to_status(esp_wifi_get_mac(WIFI_IF_AP, mac.octets.data())) : Status::not_ready;
}

Status EspWifiAccessPoint::set_default_route()
{
    return state_.load() == LinkState::up ? make_default_route(netif_) : Status::not_ready;
}

Status EspWifiAccessPoint::start(const interfaces::network::WifiAccessPointConfig& config)
{
    if (!radio_.started())
        return Status::not_ready;
    if (const Status status = core::network::check_access_point(config); !is_ok(status))
        return status;
    wifi_config_t     settings{};
    const std::size_t ssid_length = core::network::bounded_length(config.ssid, sizeof(settings.ap.ssid));
    const std::size_t password_length =
        core::network::bounded_length(config.password, sizeof(settings.ap.password) - 1);
    std::memcpy(settings.ap.ssid, config.ssid, ssid_length);
    if (password_length > 0)
        std::memcpy(settings.ap.password, config.password, password_length);
    settings.ap.ssid_len       = static_cast<std::uint8_t>(ssid_length);
    settings.ap.channel        = config.channel;
    settings.ap.max_connection = config.max_clients;
    settings.ap.ssid_hidden    = config.hidden ? 1 : 0;
    settings.ap.authmode       = password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    // Before the mode change: WIFI_EVENT_AP_START may arrive during it.
    const LinkState previous = state_.load();
    if (previous == LinkState::down)
        state_.store(LinkState::connecting);
    if (const Status status = radio_.enable_access_point(true); !is_ok(status))
    {
        state_.store(previous);
        return status;
    }
    return to_status(esp_wifi_set_config(WIFI_IF_AP, &settings));
}

Status EspWifiAccessPoint::stop()
{
    if (!radio_.started())
        return Status::not_ready;
    state_.store(LinkState::down);
    clients_.store(0);
    return radio_.enable_access_point(false);
}

void EspWifiAccessPoint::on_wifi_event(std::int32_t id)
{
    switch (id)
    {
        case WIFI_EVENT_AP_START:
            // Down: stop() was called while the hotspot was starting.
            if (state_.load() != LinkState::down)
                state_.store(LinkState::up);
            break;
        case WIFI_EVENT_AP_STOP:
            state_.store(LinkState::down);
            clients_.store(0);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            clients_.store(static_cast<std::uint8_t>(clients_.load() + 1));
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            if (clients_.load() > 0)
                clients_.store(static_cast<std::uint8_t>(clients_.load() - 1));
            break;
        default:
            break;
    }
}

}  // namespace brick::platform::esp32
