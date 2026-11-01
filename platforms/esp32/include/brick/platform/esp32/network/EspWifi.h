#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/IWifiAccessPoint.h"
#include "brick/interfaces/network/IWifiStation.h"
#include "esp_event.h"
#include "esp_netif.h"

// Wi-Fi of the ESP-IDF targets. Only esp_event/esp_netif are included here,
// so a board header can name these classes on an ESP32-P4 whose Wi-Fi
// components (esp_wifi_remote + esp_hosted) are not in the build.
namespace brick::platform::esp32
{

struct EspWifiConfig
{
    /// Regulatory domain: "01" (world safe mode) or a country such as "PL"
    /// (channels 1..13).
    const char* country_code = "01";
    /// Modem sleep between beacons: saves power, adds latency.
    bool power_save = false;
    /// Name sent to the DHCP server; nullptr keeps the ESP-IDF default.
    const char* hostname = nullptr;
};

class EspWifi;

/// Station half of EspWifi. ESP-IDF events arrive in the event task, so the
/// state shared with the loop is atomic.
class EspWifiStation final : public interfaces::network::IWifiStation
{
public:
    /// Access points kept from a scan, strongest first, one per SSID.
    static constexpr std::size_t kMaxScanResults = 20;

    explicit EspWifiStation(EspWifi& radio) : radio_(radio) {}

    const char*                    name() const override { return "wifi"; }
    interfaces::network::LinkState link_state() const override { return state_.load(); }
    Status                         ip_info(interfaces::network::IpInfo& info) const override;
    Status                         mac_address(interfaces::network::MacAddress& mac) const override;
    Status                         set_default_route() override;

    Status start_scan() override;
    Status scan_results(interfaces::network::WifiNetwork* networks, std::size_t capacity, std::size_t& count) override;
    Status connect(const interfaces::network::WifiCredentials& credentials) override;
    Status disconnect() override;
    interfaces::network::WifiFailure failure() const override { return failure_.load(); }
    Status                           connected_network(interfaces::network::WifiNetwork& network) const override;

    /// Used by EspWifi: the netif and the events of the station.
    void         attach(esp_netif_t* netif) { netif_ = netif; }
    esp_netif_t* netif() const { return netif_; }
    void         on_wifi_event(std::int32_t id, const void* data);
    void         on_ip_event(std::int32_t id);

private:
    enum class ScanState : std::uint8_t
    {
        idle,
        running,
        done,
        failed,
    };

    void      collect_scan_();
    esp_err_t leave_();
    void      on_disconnected_(std::uint16_t reason);

    EspWifi&                                      radio_;
    esp_netif_t*                                  netif_ = nullptr;
    std::atomic<interfaces::network::LinkState>   state_{ interfaces::network::LinkState::down };
    std::atomic<interfaces::network::WifiFailure> failure_{ interfaces::network::WifiFailure::none };
    std::atomic<ScanState>                        scan_{ ScanState::idle };
    /// Set when this driver disconnects, so that its own disconnect event is no failure.
    std::atomic<bool>                                             leaving_{ false };
    std::array<interfaces::network::WifiNetwork, kMaxScanResults> scan_results_{};
    std::size_t                                                   scan_count_ = 0;
    bool                                                          collected_  = false;
};

/// Hotspot half of EspWifi; runs alongside the station (APSTA mode).
class EspWifiAccessPoint final : public interfaces::network::IWifiAccessPoint
{
public:
    explicit EspWifiAccessPoint(EspWifi& radio) : radio_(radio) {}

    const char*                    name() const override { return "access point"; }
    interfaces::network::LinkState link_state() const override { return state_.load(); }
    Status                         ip_info(interfaces::network::IpInfo& info) const override;
    Status                         mac_address(interfaces::network::MacAddress& mac) const override;
    Status                         set_default_route() override;

    Status      start(const interfaces::network::WifiAccessPointConfig& config) override;
    Status      stop() override;
    std::size_t client_count() const override { return clients_.load(); }

    /// Used by EspWifi: the netif and the events of the hotspot.
    void         attach(esp_netif_t* netif) { netif_ = netif; }
    esp_netif_t* netif() const { return netif_; }
    void         on_wifi_event(std::int32_t id);

private:
    EspWifi&                                    radio_;
    esp_netif_t*                                netif_ = nullptr;
    std::atomic<interfaces::network::LinkState> state_{ interfaces::network::LinkState::down };
    std::atomic<std::uint8_t>                   clients_{ 0 };
};

/// The Wi-Fi radio: owns the ESP-IDF driver, its netifs and event handlers,
/// and offers its station and hotspot. On an ESP32-P4 the radio is an
/// ESP32-C6 on SDIO, reached through the esp_hosted and esp_wifi_remote
/// components; the API is the same.
class EspWifi final
{
public:
    explicit EspWifi(EspWifiConfig config = {}) : config_(config), station_(*this), access_point_(*this) {}
    ~EspWifi();
    EspWifi(const EspWifi&)            = delete;
    EspWifi& operator=(const EspWifi&) = delete;

    /// Starts the radio as a station (no network joined yet).
    Status              begin();
    bool                started() const { return started_; }
    EspWifiStation&     station() { return station_; }
    EspWifiAccessPoint& access_point() { return access_point_; }

    /// Turns the hotspot half of the radio on or off; used by EspWifiAccessPoint.
    Status enable_access_point(bool enabled);

private:
    static void on_event_(void* context, esp_event_base_t base, std::int32_t id, void* data);
    Status      create_netifs_();
    Status      start_driver_();
    void        apply_settings_();

    EspWifiConfig                config_;
    EspWifiStation               station_;
    EspWifiAccessPoint           access_point_;
    esp_event_handler_instance_t wifi_events_ = nullptr;
    esp_event_handler_instance_t ip_events_   = nullptr;
    bool                         started_     = false;
};

}  // namespace brick::platform::esp32
