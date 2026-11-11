#include "brick/core/network/WifiSetupPortal.h"

#include <cstring>

#include "brick/core/network/Ipv4.h"
#include "brick/core/network/WifiConfigCheck.h"

namespace brick::core::network
{

using interfaces::network::LinkState;

namespace
{
    constexpr char kTag[] = "brick_wifi_setup";

    constexpr char kStyle[] = "<style>body{font-family:sans-serif;margin:1em auto;max-width:28em;padding:0 1em}"
                              "input,select,button{display:block;width:100%;box-sizing:border-box;font-size:1em;"
                              "padding:.5em;margin:.3em 0 1em}</style>";
}  // namespace

WifiSetupPortal::WifiSetupPortal(WifiManager& wifi, WifiCredentialStore& store,
                                 interfaces::network::IWifiStation&     station,
                                 interfaces::network::IWifiAccessPoint& access_point,
                                 interfaces::network::ITcpListener&     listener,
                                 interfaces::network::IUdpSocket* dns_socket, interfaces::time::ITimeProvider& time,
                                 interfaces::logging::ILogger& logger)
    : wifi_(wifi), store_(store), station_(station), access_point_(access_point), logger_(logger),
      http_(listener, time, logger, *this, request_.data(), request_.size(), response_.data(), response_.size())
{
    if (dns_socket != nullptr)
        dns_.emplace(*dns_socket, logger);
}

Status WifiSetupPortal::start(const WifiSetupPortalConfig& config)
{
    config_ = config;
    action_ = Action::none;
    return http_.start(config.http_port);
}

void WifiSetupPortal::stop()
{
    http_.stop();
    if (dns_)
        dns_->stop();
}

void WifiSetupPortal::update()
{
    if (!http_.running())
        return;
    http_.update();
    // The answer to the form goes out before the radio changes networks.
    if (!http_.busy())
        run_action_();
    follow_hotspot_();
    if (dns_)
        dns_->update();
}

void WifiSetupPortal::handle(const HttpRequest& request, HttpResponse& response)
{
    // A name that is not an address came through the captive DNS: a phone's
    // connectivity check or a page the user typed. Send it to the setup page.
    interfaces::network::Ipv4Address address;
    char                             url[32];
    if (request.host[0] != '\0' && !parse_ipv4(request.host, address) && hotspot_url_(url, sizeof(url)))
    {
        response.redirect(url);
        return;
    }
    if (std::strcmp(request.path, "/") == 0 && request.method == HttpMethod::get)
        page_(response);
    else if (std::strcmp(request.path, "/scan") == 0 && request.method == HttpMethod::get)
    {
        (void)wifi_.request_scan();
        response.redirect("/");
    }
    else if (std::strcmp(request.path, "/connect") == 0 && request.method == HttpMethod::post)
        connect_(request, response);
    else if (std::strcmp(request.path, "/forget") == 0 && request.method == HttpMethod::post)
        forget_(response);
}

void WifiSetupPortal::head_(HttpResponse& response)
{
    response.start(200);
    response.text("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
    response.escaped(config_.title).text(" Wi-Fi</title>").text(kStyle).text("</head><body><h1>");
    response.escaped(config_.title).text("</h1>");
}

void WifiSetupPortal::page_(HttpResponse& response)
{
    head_(response);
    status_(response);
    response.text("<form method=\"post\" action=\"/connect\"><label>Network<select name=\"ssid\">");
    networks_(response);
    response.text("</select></label><label>Or a hidden network<input name=\"other\" maxlength=\"32\"></label>"
                  "<label>Password<input name=\"password\" type=\"password\" maxlength=\"63\"></label>"
                  "<button>Connect</button></form><p><a href=\"/scan\">Scan again</a></p>");
    if (const auto* network = store_.network(); network != nullptr)
        response.text("<form method=\"post\" action=\"/forget\"><button>Forget ")
            .escaped(network->ssid)
            .text("</button></form>");
    response.text("</body></html>");
}

void WifiSetupPortal::status_(HttpResponse& response)
{
    const auto* network = wifi_.current_network();
    response.text("<p>");
    switch (wifi_.state())
    {
        case WifiManagerState::connected:
        {
            interfaces::network::IpInfo info;
            char                        address[kIpv4TextCapacity] = "?";
            if (is_ok(station_.ip_info(info)))
                (void)format_ipv4(info.address, address, sizeof(address));
            response.text("Connected to <b>").escaped(network->ssid).text("</b>, address ").text(address);
            break;
        }
        case WifiManagerState::connecting:
            response.text("Joining <b>").escaped(network->ssid).text("</b>");
            break;
        default:
            if (wifi_.known_network_count() == 0)
                response.text("No network set up yet");
            else if (wifi_.last_failure() != interfaces::network::WifiFailure::none)
                response.text("Not connected: ").text(to_string(wifi_.last_failure())).text("; still trying");
            else
                response.text("Looking for the network");
            break;
    }
    response.text(".</p>");
}

void WifiSetupPortal::networks_(HttpResponse& response)
{
    const auto* current = wifi_.current_network();
    std::size_t shown   = 0;
    for (std::size_t index = 0; index < wifi_.network_count(); ++index)
    {
        const auto& network = wifi_.networks()[index];
        if (network.ssid[0] == '\0')
            continue;  // hidden: typed in below
        response.text("<option value=\"").escaped(network.ssid).text("\"");
        if (current != nullptr && same_ssid(current->ssid, network.ssid))
            response.text(" selected");
        response.text(">").escaped(network.ssid).text(" (").number(network.rssi_dbm).text(" dBm");
        response.text(network.security == interfaces::network::WifiSecurity::open ? ", open)</option>" : ")</option>");
        ++shown;
    }
    if (shown == 0)
        response.text("<option value=\"\">No networks found yet</option>");
}

void WifiSetupPortal::connect_(const HttpRequest& request, HttpResponse& response)
{
    char ssid[interfaces::network::kWifiSsidMaxLength + 1];
    char other[interfaces::network::kWifiSsidMaxLength + 1];
    char password[interfaces::network::kWifiPasswordMaxLength + 1];
    (void)http_form_value(request.body, "ssid", ssid, sizeof(ssid));
    (void)http_form_value(request.body, "other", other, sizeof(other));
    (void)http_form_value(request.body, "password", password, sizeof(password));
    const bool                           hidden = other[0] != '\0';
    interfaces::network::WifiCredentials credentials{ hidden ? other : ssid, password, hidden };
    const Status                         status = store_.save(credentials);
    head_(response);
    if (!is_ok(status))
    {
        response.text("<p>Not saved: ")
            .text(status == Status::invalid_argument ?
                      "choose a network; the password has 8 to 63 characters (or none for an open network)" :
                      to_string(status))
            .text(".</p><p><a href=\"/\">Back</a></p></body></html>");
        return;
    }
    logger_.info(kTag, "Network %s set on the setup page", credentials.ssid);
    action_ = Action::join;
    response.text("<p>Joining <b>").escaped(credentials.ssid);
    response.text("</b>. The hotspot closes once the device is connected; this page is then at the device's "
                  "address in that network (its screen shows it).</p><p><a href=\"/\">Status</a></p></body></html>");
}

void WifiSetupPortal::forget_(HttpResponse& response)
{
    const Status status = store_.forget();
    head_(response);
    if (!is_ok(status))
    {
        response.text("<p>Not forgotten: ").text(to_string(status)).text(".</p></body></html>");
        return;
    }
    logger_.info(kTag, "Network forgotten on the setup page");
    action_ = Action::forget;
    response.text("<p>Network forgotten. The device leaves it and opens its hotspot again; join the hotspot "
                  "to set up a network.</p></body></html>");
}

Status WifiSetupPortal::forget_network()
{
    if (const Status status = store_.forget(); !is_ok(status))
        return status;
    action_ = Action::none;
    logger_.info(kTag, "Network forgotten");
    return wifi_.set_networks(nullptr, 0);
}

void WifiSetupPortal::run_action_()
{
    const Action action = action_;
    action_             = Action::none;
    if (action == Action::none)
        return;
    const auto*  network = action == Action::join ? store_.network() : nullptr;
    const Status status  = wifi_.set_networks(network, network == nullptr ? 0 : 1);
    if (!is_ok(status))
        logger_.error(kTag, "Changing the network failed: %s", to_string(status));
}

void WifiSetupPortal::follow_hotspot_()
{
    if (!dns_)
        return;
    const bool up = access_point_.link_state() == LinkState::up;
    if (up && !dns_->running())
    {
        interfaces::network::IpInfo info;
        if (!is_ok(access_point_.ip_info(info)))
            return;
        if (const Status status = dns_->start(info.address); !is_ok(status))
            logger_.warning(kTag, "Captive DNS not started: %s", to_string(status));
    }
    else if (!up && dns_->running())
        dns_->stop();
}

bool WifiSetupPortal::hotspot_url_(char* text, std::size_t capacity) const
{
    interfaces::network::IpInfo info;
    char                        address[kIpv4TextCapacity];
    if (access_point_.link_state() != LinkState::up || !is_ok(access_point_.ip_info(info))
        || !is_ok(format_ipv4(info.address, address, sizeof(address))))
        return false;
    HttpResponse url(text, capacity);
    url.text("http://").text(address).text("/");
    return !url.truncated();
}

}  // namespace brick::core::network
