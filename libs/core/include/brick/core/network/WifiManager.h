#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/core/network/WifiNetworkChoice.h"
#include "brick/core/time/Timer.h"
#include "brick/interfaces/logging/ILogger.h"
#include "brick/interfaces/network/IWifiAccessPoint.h"
#include "brick/interfaces/network/IWifiStation.h"
#include "brick/interfaces/time/ITimeProvider.h"

namespace brick::core::network
{

struct WifiManagerConfig
{
    /// Known networks. The array and its strings must stay valid while the
    /// manager runs.
    const interfaces::network::WifiCredentials* networks           = nullptr;
    std::size_t                                 network_count      = 0;
    std::uint32_t                               scan_timeout_ms    = 15000;
    std::uint32_t                               connect_timeout_ms = 20000;  ///< joining plus DHCP
    std::uint32_t retry_delay_ms     = 2000;  ///< pause after the first failure, doubled after every next one
    std::uint32_t max_retry_delay_ms = 60000;
    /// Hotspot opened after attempts_before_fallback failures in a row, or at
    /// once without known networks (nullptr: never). It closes when the
    /// station connects again; until then the station keeps trying.
    const interfaces::network::WifiAccessPointConfig* fallback_access_point    = nullptr;
    std::uint8_t                                      attempts_before_fallback = 3;
};

enum class WifiManagerState : std::uint8_t
{
    stopped,
    scanning,
    connecting,
    connected,
    waiting,  ///< pause before the next attempt
};

constexpr const char* to_string(WifiManagerState state)
{
    switch (state)
    {
        case WifiManagerState::stopped:
            return "stopped";
        case WifiManagerState::scanning:
            return "scanning";
        case WifiManagerState::connecting:
            return "connecting";
        case WifiManagerState::connected:
            return "connected";
        case WifiManagerState::waiting:
            return "waiting";
    }
    return "unknown";
}

/// Keeps a Wi-Fi station connected to the best known network: scans, joins
/// the strongest known network in range, retries with growing pauses,
/// rejoins after a loss and can open a hotspot while nothing works (for
/// example to let a user enter credentials). Works with any IWifiStation;
/// call update() from the loop, it never blocks.
class WifiManager final
{
public:
    /// Networks kept from a scan, strongest first.
    static constexpr std::size_t kMaxNetworks = 16;

    WifiManager(interfaces::network::IWifiStation& station, interfaces::time::ITimeProvider& time,
                interfaces::logging::ILogger& logger, interfaces::network::IWifiAccessPoint* access_point = nullptr);

    /// Starts connecting. Status::invalid_argument for an invalid credential
    /// or hotspot, no network without a hotspot, or a hotspot without an
    /// access point.
    Status start(const WifiManagerConfig& config);
    /// Leaves the network and closes the hotspot.
    void stop();
    void update();
    /// Scans again while connected or waiting, e.g. for a list on the screen;
    /// networks() changes when scan_count() does. Status::busy while joining
    /// or scanning, Status::not_ready when stopped.
    Status request_scan();

    WifiManagerState state() const { return state_; }
    /// The network being joined or joined; nullptr otherwise.
    const interfaces::network::WifiCredentials* current_network() const;
    interfaces::network::WifiFailure            last_failure() const { return last_failure_; }
    /// Failed attempts since the last successful connection.
    std::uint16_t                           failed_attempts() const { return failed_attempts_; }
    bool                                    access_point_active() const { return access_point_active_; }
    const interfaces::network::WifiNetwork* networks() const { return networks_.data(); }
    std::size_t                             network_count() const { return network_count_; }
    /// Number of completed scans.
    std::uint32_t scan_count() const { return scan_count_; }

private:
    Status        validate_(const WifiManagerConfig& config) const;
    void          enter_scanning_();
    void          update_scanning_();
    void          enter_connecting_(std::size_t index);
    void          update_connecting_();
    void          on_connected_();
    void          update_connected_();
    void          update_waiting_();
    void          fail_(interfaces::network::WifiFailure failure);
    Status        store_scan_();
    void          poll_requested_scan_();
    std::uint32_t retry_delay_() const;
    void          open_fallback_();
    void          close_fallback_();

    interfaces::network::IWifiStation&                         station_;
    interfaces::network::IWifiAccessPoint*                     access_point_;
    interfaces::logging::ILogger&                              logger_;
    core::time::Timer                                          timer_;
    WifiManagerConfig                                          config_{};
    std::array<interfaces::network::WifiNetwork, kMaxNetworks> networks_{};
    std::size_t                                                network_count_ = 0;
    std::uint32_t                                              scan_count_    = 0;
    WifiManagerState                                           state_         = WifiManagerState::stopped;
    interfaces::network::WifiFailure                           last_failure_  = interfaces::network::WifiFailure::none;
    std::uint16_t                                              failed_attempts_     = 0;
    std::size_t                                                current_             = kNoWifiNetwork;
    std::size_t                                                last_failed_         = kNoWifiNetwork;
    bool                                                       scan_requested_      = false;
    bool                                                       access_point_active_ = false;
};

}  // namespace brick::core::network
