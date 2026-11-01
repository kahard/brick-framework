#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "brick/interfaces/network/IWifiAccessPoint.h"
#include "brick/interfaces/network/IWifiStation.h"
#include "brick/platform/pc/PcTime.h"

namespace brick::platform::pc
{

/// An access point the simulated radio "finds".
struct PcWifiNetwork
{
    std::string  ssid;
    std::string  password;  ///< empty: open network
    std::int8_t  rssi_dbm = -60;
    std::uint8_t channel  = 6;
};

/// Simulated station. Everything happens in simulated time, so headless runs
/// with --manual-time are deterministic: a scan takes kScanMs, joining with
/// the right password kJoinMs, a wrong password fails after kJoinMs and an
/// SSID that is not in range after kNotFoundMs. Traffic goes through the PC's
/// own network.
class PcWifiStation final : public brick::interfaces::network::IWifiStation
{
public:
    static constexpr std::uint32_t kScanMs     = 1200;
    static constexpr std::uint32_t kJoinMs     = 1500;
    static constexpr std::uint32_t kNotFoundMs = 3000;

    PcWifiStation(PcTime& time, const std::vector<PcWifiNetwork>& networks) : time_(time), networks_(networks) {}

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
    std::uint32_t                           elapsed_ms_(std::uint32_t since_ms) const;
    std::uint32_t                           outcome_delay_ms_() const;
    brick::interfaces::network::WifiNetwork to_network_(std::size_t index) const;

    PcTime&                                 time_;
    const std::vector<PcWifiNetwork>&       networks_;
    bool                                    scanning_      = false;
    std::uint32_t                           scan_start_ms_ = 0;
    bool                                    joining_       = false;
    std::uint32_t                           join_start_ms_ = 0;
    std::size_t                             target_        = 0;
    brick::interfaces::network::WifiFailure outcome_       = brick::interfaces::network::WifiFailure::none;
};

/// Simulated hotspot: up right after start(), at 192.168.4.1, without clients.
class PcWifiAccessPoint final : public brick::interfaces::network::IWifiAccessPoint
{
public:
    const char*                           name() const override { return "access point"; }
    brick::interfaces::network::LinkState link_state() const override;
    Status                                ip_info(brick::interfaces::network::IpInfo& info) const override;
    Status                                mac_address(brick::interfaces::network::MacAddress& mac) const override;
    Status                                set_default_route() override;

    Status      start(const brick::interfaces::network::WifiAccessPointConfig& config) override;
    Status      stop() override;
    std::size_t client_count() const override { return 0; }

    /// SSID of the running hotspot, empty when stopped.
    const std::string& ssid() const { return ssid_; }

private:
    std::string ssid_;
    bool        running_ = false;
};

/// Simulated Wi-Fi radio with its station and hotspot, like EspWifi.
class PcWifi final
{
public:
    /// networks: what a scan finds; default_networks() when empty.
    PcWifi(PcTime& time, std::vector<PcWifiNetwork> networks);

    Status             begin() { return Status::ok; }
    PcWifiStation&     station() { return station_; }
    PcWifiAccessPoint& access_point() { return access_point_; }

    /// "BRICK-LAB" (password "brick-lab-pass", the strongest), a neighbour's
    /// network and an open one.
    static std::vector<PcWifiNetwork> default_networks();

private:
    std::vector<PcWifiNetwork> networks_;
    PcWifiStation              station_;
    PcWifiAccessPoint          access_point_;
};

}  // namespace brick::platform::pc
