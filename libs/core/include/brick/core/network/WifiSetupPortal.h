#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "brick/Status.h"
#include "brick/core/network/CaptiveDns.h"
#include "brick/core/network/HttpServer.h"
#include "brick/core/network/WifiCredentialStore.h"
#include "brick/core/network/WifiManager.h"
#include "brick/interfaces/logging/ILogger.h"
#include "brick/interfaces/network/ITcpListener.h"
#include "brick/interfaces/network/IUdpSocket.h"
#include "brick/interfaces/network/IWifiAccessPoint.h"
#include "brick/interfaces/network/IWifiStation.h"
#include "brick/interfaces/time/ITimeProvider.h"

namespace brick::core::network
{

struct WifiSetupPortalConfig
{
    const char*   title     = "BRICK";  ///< the device's name on the page
    std::uint16_t http_port = 80;
};

/// Wi-Fi setup page of a device. It is served on the hotspot, where a DNS
/// that points every name at the device makes a phone open it by itself
/// (captive portal), and on the joined network, at the device's address. The
/// page lists the networks of the last scan and takes a network and its
/// password: they go to the store and the manager joins them (the hotspot
/// closes once that works). "Forget" stores "no network" and opens the
/// hotspot again. Call update() from the loop.
class WifiSetupPortal final : public IHttpHandler
{
public:
    static constexpr std::size_t kRequestBytes  = 1024;
    static constexpr std::size_t kResponseBytes = 3072;

    /// dns_socket: UDP socket for the captive DNS, nullptr for none (the page
    /// is then at http://<hotspot address>/ only).
    WifiSetupPortal(WifiManager& wifi, WifiCredentialStore& store, interfaces::network::IWifiStation& station,
                    interfaces::network::IWifiAccessPoint& access_point, interfaces::network::ITcpListener& listener,
                    interfaces::network::IUdpSocket* dns_socket, interfaces::time::ITimeProvider& time,
                    interfaces::logging::ILogger& logger);

    /// Starts the web server; the DNS follows the hotspot in update().
    Status start(const WifiSetupPortalConfig& config = {});
    void   stop();
    void   update();
    bool   running() const { return http_.running(); }
    /// What "Forget" on the page does, for a button held down: stores "no
    /// network", leaves the network and opens the hotspot.
    Status forget_network();

    void handle(const HttpRequest& request, HttpResponse& response) override;

private:
    enum class Action : std::uint8_t
    {
        none,
        join,
        forget,
    };

    void page_(HttpResponse& response);
    void status_(HttpResponse& response);
    void networks_(HttpResponse& response);
    void head_(HttpResponse& response);
    void connect_(const HttpRequest& request, HttpResponse& response);
    void forget_(HttpResponse& response);
    void run_action_();
    void follow_hotspot_();
    /// "http://a.b.c.d/" of the hotspot; false while it is down.
    bool hotspot_url_(char* text, std::size_t capacity) const;

    WifiManager&                           wifi_;
    WifiCredentialStore&                   store_;
    interfaces::network::IWifiStation&     station_;
    interfaces::network::IWifiAccessPoint& access_point_;
    interfaces::logging::ILogger&          logger_;
    WifiSetupPortalConfig                  config_{};
    std::array<char, kRequestBytes>        request_{};
    std::array<char, kResponseBytes>       response_{};
    HttpServer                             http_;
    std::optional<CaptiveDns>              dns_;
    Action                                 action_ = Action::none;
};

}  // namespace brick::core::network
