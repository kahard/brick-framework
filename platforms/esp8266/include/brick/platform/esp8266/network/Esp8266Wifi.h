#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/IWifiAccessPoint.h"
#include "brick/interfaces/network/IWifiStation.h"

// Wi-Fi of the ESP8266 over the Arduino ESP8266WiFi library. The ESP8266 runs
// the application and the network stack in one cooperative context, so the
// state needs no locking.
namespace brick::platform::esp8266
{

struct Esp8266WifiConfig
{
    /// Name sent to the DHCP server; nullptr keeps the default ("ESP-xxxxxx").
    const char* hostname = nullptr;
    /// Modem sleep between beacons: saves power, adds latency.
    bool power_save = false;
};

class Esp8266Wifi;

/// Station half of Esp8266Wifi; it does not reconnect by itself.
class Esp8266WifiStation final : public brick::interfaces::network::IWifiStation
{
public:
    static constexpr std::size_t kMaxScanResults = 16;

    explicit Esp8266WifiStation(Esp8266Wifi& radio) : radio_(radio) {}

    const char*                           name() const override { return "wifi"; }
    brick::interfaces::network::LinkState link_state() const override;
    Status                                ip_info(brick::interfaces::network::IpInfo& info) const override;
    Status                                mac_address(brick::interfaces::network::MacAddress& mac) const override;
    Status                                set_default_route() override;

    Status start_scan() override;
    Status scan_results(brick::interfaces::network::WifiNetwork* networks, std::size_t capacity,
                        std::size_t& count) override;
    Status connect(const brick::interfaces::network::WifiCredentials& credentials) override;
    Status disconnect() override;
    brick::interfaces::network::WifiFailure failure() const override;
    Status connected_network(brick::interfaces::network::WifiNetwork& network) const override;

private:
    enum class ScanState : std::uint8_t
    {
        idle,
        running,
        done,
        failed,
    };

    Status collect_scan_();

    Esp8266Wifi& radio_;
    bool         joining_ = false;
    /// Set once the attempt reached an address, so a later drop reads as a loss.
    mutable bool                                                         was_up_ = false;
    ScanState                                                            scan_   = ScanState::idle;
    std::array<brick::interfaces::network::WifiNetwork, kMaxScanResults> scan_results_{};
    std::size_t                                                          scan_count_ = 0;
};

/// Hotspot half of Esp8266Wifi; runs alongside the station.
class Esp8266WifiAccessPoint final : public brick::interfaces::network::IWifiAccessPoint
{
public:
    explicit Esp8266WifiAccessPoint(Esp8266Wifi& radio) : radio_(radio) {}

    const char*                           name() const override { return "access point"; }
    brick::interfaces::network::LinkState link_state() const override;
    Status                                ip_info(brick::interfaces::network::IpInfo& info) const override;
    Status                                mac_address(brick::interfaces::network::MacAddress& mac) const override;
    Status                                set_default_route() override;

    Status      start(const brick::interfaces::network::WifiAccessPointConfig& config) override;
    Status      stop() override;
    std::size_t client_count() const override;

private:
    Esp8266Wifi& radio_;
    bool         running_ = false;
};

/// The Wi-Fi radio of the ESP8266 with its station and hotspot.
class Esp8266Wifi final
{
public:
    explicit Esp8266Wifi(Esp8266WifiConfig config = {}) : config_(config), station_(*this), access_point_(*this) {}

    /// Switches the radio on as a station (no network joined yet).
    Status                  begin();
    bool                    started() const { return started_; }
    Esp8266WifiStation&     station() { return station_; }
    Esp8266WifiAccessPoint& access_point() { return access_point_; }

private:
    Esp8266WifiConfig      config_;
    Esp8266WifiStation     station_;
    Esp8266WifiAccessPoint access_point_;
    bool                   started_ = false;
};

}  // namespace brick::platform::esp8266
