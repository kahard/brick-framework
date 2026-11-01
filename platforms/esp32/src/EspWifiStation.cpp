#include <cstring>

#include "brick/core/network/WifiConfigCheck.h"
#include "brick/core/network/WifiScanList.h"
#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/network/EspNetworkStack.h"
#include "brick/platform/esp32/network/EspWifi.h"
#include "esp_log.h"
#include "esp_wifi.h"

namespace brick::platform::esp32
{

using interfaces::network::LinkState;
using interfaces::network::WifiFailure;
using interfaces::network::WifiNetwork;
using interfaces::network::WifiSecurity;

namespace
{
    constexpr char kTag[] = "brick_wifi";
    /// Records read from one scan; the driver frees the rest.
    constexpr std::size_t kMaxScanRecords = 64;

    WifiSecurity to_security(wifi_auth_mode_t mode)
    {
        switch (mode)
        {
            case WIFI_AUTH_OPEN:
                return WifiSecurity::open;
            case WIFI_AUTH_WEP:
                return WifiSecurity::wep;
            case WIFI_AUTH_WPA_PSK:
                return WifiSecurity::wpa;
            case WIFI_AUTH_WPA2_PSK:
            case WIFI_AUTH_WPA_WPA2_PSK:
                return WifiSecurity::wpa2;
            case WIFI_AUTH_WPA3_PSK:
                return WifiSecurity::wpa3;
            case WIFI_AUTH_WPA2_WPA3_PSK:
                return WifiSecurity::wpa2_wpa3;
            case WIFI_AUTH_ENTERPRISE:
            case WIFI_AUTH_WPA3_ENTERPRISE:
            case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
            case WIFI_AUTH_WPA_ENTERPRISE:
            case WIFI_AUTH_WPA3_ENT_192:
                return WifiSecurity::enterprise;
            default:
                return WifiSecurity::unknown;
        }
    }

    WifiNetwork to_network(const wifi_ap_record_t& record)
    {
        WifiNetwork network;
        std::memcpy(network.ssid, record.ssid, sizeof(network.ssid) - 1);
        network.ssid[sizeof(network.ssid) - 1] = '\0';
        std::memcpy(network.bssid.octets.data(), record.bssid, network.bssid.octets.size());
        network.rssi_dbm = record.rssi;
        network.channel  = record.primary;
        network.security = to_security(record.authmode);
        return network;
    }

    WifiFailure to_failure(std::uint16_t reason, bool was_connected)
    {
        if (was_connected)
            return WifiFailure::connection_lost;
        switch (reason)
        {
            case WIFI_REASON_NO_AP_FOUND:
            case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
                return WifiFailure::network_not_found;
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_MIC_FAILURE:
                return WifiFailure::authentication_failed;
            default:
                return WifiFailure::other;
        }
    }

    /// Copies a checked SSID or password into a fixed ESP-IDF field.
    template <std::size_t Size>
    void copy_field(std::uint8_t (&field)[Size], const char* text)
    {
        std::memcpy(field, text, core::network::bounded_length(text, Size - 1));
    }
}  // namespace

Status EspWifiStation::ip_info(interfaces::network::IpInfo& info) const
{
    return state_.load() == LinkState::up ? read_ip_info(netif_, info) : Status::not_ready;
}

Status EspWifiStation::mac_address(interfaces::network::MacAddress& mac) const
{
    return radio_.started() ? to_status(esp_wifi_get_mac(WIFI_IF_STA, mac.octets.data())) : Status::not_ready;
}

Status EspWifiStation::set_default_route()
{
    return state_.load() == LinkState::up ? make_default_route(netif_) : Status::not_ready;
}

Status EspWifiStation::start_scan()
{
    if (!radio_.started())
        return Status::not_ready;
    if (scan_.load() == ScanState::running)
        return Status::busy;
    collected_  = false;
    scan_count_ = 0;
    scan_.store(ScanState::running);
    wifi_scan_config_t config{};
    config.show_hidden    = true;
    const esp_err_t error = esp_wifi_scan_start(&config, false);
    if (error == ESP_OK)
        return Status::ok;
    scan_.store(ScanState::idle);
    // ESP_ERR_WIFI_STATE: the station is joining a network; scan after that.
    return error == ESP_ERR_WIFI_STATE ? Status::busy : to_status(error);
}

Status EspWifiStation::scan_results(WifiNetwork* networks, std::size_t capacity, std::size_t& count)
{
    count = 0;
    switch (scan_.load())
    {
        case ScanState::idle:
            return Status::not_ready;
        case ScanState::running:
            return Status::busy;
        case ScanState::failed:
            return Status::error;
        case ScanState::done:
            break;
    }
    if (networks == nullptr && capacity > 0)
        return Status::invalid_argument;
    if (!collected_)
        collect_scan_();
    for (; count < capacity && count < scan_count_; ++count)
        networks[count] = scan_results_[count];
    return Status::ok;
}

void EspWifiStation::collect_scan_()
{
    scan_count_ = 0;
    wifi_ap_record_t record{};
    for (std::size_t read = 0; read < kMaxScanRecords && esp_wifi_scan_get_ap_record(&record) == ESP_OK; ++read)
        core::network::add_scanned_network(scan_results_.data(), scan_results_.size(), scan_count_, to_network(record));
    // Frees the records of a scan that found more access points than were read.
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_clear_ap_list());
    collected_ = true;
}

Status EspWifiStation::connect(const interfaces::network::WifiCredentials& credentials)
{
    if (!radio_.started())
        return Status::not_ready;
    if (const Status status = core::network::check_credentials(credentials); !is_ok(status))
        return status;
    wifi_config_t config{};
    copy_field(config.sta.ssid, credentials.ssid);
    copy_field(config.sta.password, credentials.password);
    const bool open = core::network::bounded_length(credentials.password, 1) == 0;
    // WPA_PSK as the weakest accepted mode lets WPA, WPA2 and WPA3 networks in.
    config.sta.threshold.authmode = open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    config.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method        = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.sae_pwe_h2e        = WPA3_SAE_PWE_BOTH;
    if (const esp_err_t error = leave_(); error != ESP_OK && error != ESP_ERR_WIFI_NOT_CONNECT)
        ESP_LOGW(kTag, "Leaving the previous network failed: %s", esp_err_to_name(error));
    if (const esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &config); error != ESP_OK)
        return to_status(error);
    failure_.store(WifiFailure::none);
    state_.store(LinkState::connecting);
    if (const esp_err_t error = esp_wifi_connect(); error != ESP_OK)
    {
        state_.store(LinkState::down);
        return error == ESP_ERR_WIFI_STATE ? Status::busy : to_status(error);
    }
    return Status::ok;
}

Status EspWifiStation::disconnect()
{
    if (!radio_.started())
        return Status::not_ready;
    const esp_err_t error = leave_();
    failure_.store(WifiFailure::none);
    state_.store(LinkState::down);
    return error == ESP_OK || error == ESP_ERR_WIFI_NOT_CONNECT ? Status::ok : to_status(error);
}

esp_err_t EspWifiStation::leave_()
{
    if (state_.load() == LinkState::down)
        return ESP_OK;
    leaving_.store(true);
    return esp_wifi_disconnect();
}

Status EspWifiStation::connected_network(WifiNetwork& network) const
{
    if (state_.load() == LinkState::down)
        return Status::not_ready;
    wifi_ap_record_t record{};
    const esp_err_t  error = esp_wifi_sta_get_ap_info(&record);
    if (error == ESP_ERR_WIFI_NOT_CONNECT)
        return Status::not_ready;
    if (error != ESP_OK)
        return to_status(error);
    network = to_network(record);
    return Status::ok;
}

void EspWifiStation::on_wifi_event(std::int32_t id, const void* data)
{
    if (id == WIFI_EVENT_SCAN_DONE)
    {
        const auto* done = static_cast<const wifi_event_sta_scan_done_t*>(data);
        // Only a scan started by start_scan(); joining scans by itself.
        if (scan_.load() == ScanState::running)
            scan_.store(done->status == 0 ? ScanState::done : ScanState::failed);
    }
    else if (id == WIFI_EVENT_STA_DISCONNECTED)
    {
        on_disconnected_(static_cast<const wifi_event_sta_disconnected_t*>(data)->reason);
    }
}

void EspWifiStation::on_disconnected_(std::uint16_t reason)
{
    // The event of our own esp_wifi_disconnect() is no failure.
    if (reason == WIFI_REASON_ASSOC_LEAVE && leaving_.load())
    {
        leaving_.store(false);
        return;
    }
    const LinkState previous = state_.load();
    if (previous == LinkState::down)
        return;
    ESP_LOGW(kTag, "Disconnected, reason %u", static_cast<unsigned>(reason));
    // failure_ first: the loop reads the state, then the failure.
    failure_.store(to_failure(reason, previous == LinkState::up));
    state_.store(LinkState::down);
}

void EspWifiStation::on_ip_event(std::int32_t id)
{
    if (id == IP_EVENT_STA_GOT_IP && state_.load() != LinkState::down)
        state_.store(LinkState::up);
    else if (id == IP_EVENT_STA_LOST_IP && state_.load() == LinkState::up)
        state_.store(LinkState::connecting);
}

}  // namespace brick::platform::esp32
