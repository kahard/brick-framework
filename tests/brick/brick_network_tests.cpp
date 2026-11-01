// Tests of the portable network code: address formatting, credential checks,
// the Wi-Fi manager, the network selector and the HTTP probe, all against
// the fakes in brick/mocks/NetworkFakes.h.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "brick/core/network/HttpProbe.h"
#include "brick/core/network/Ipv4.h"
#include "brick/core/network/NetworkSelector.h"
#include "brick/core/network/WifiConfigCheck.h"
#include "brick/core/network/WifiManager.h"
#include "brick/core/network/WifiNetworkChoice.h"
#include "brick/core/network/WifiScanList.h"
#include "brick/mocks/Fakes.h"
#include "brick/mocks/NetworkFakes.h"

namespace
{

using brick::Status;
using brick::core::network::WifiManager;
using brick::core::network::WifiManagerConfig;
using brick::core::network::WifiManagerState;
using brick::interfaces::network::Ipv4Address;
using brick::interfaces::network::LinkState;
using brick::interfaces::network::WifiAccessPointConfig;
using brick::interfaces::network::WifiCredentials;
using brick::interfaces::network::WifiFailure;
using brick::mocks::FakeClock;
using brick::mocks::FakeWifiAccessPoint;
using brick::mocks::FakeWifiStation;
using brick::mocks::RecordingLogger;

void test_ipv4_text()
{
    char text[brick::core::network::kIpv4TextCapacity];
    assert(brick::core::network::format_ipv4({ { 192, 168, 4, 1 } }, text, sizeof(text)) == Status::ok);
    assert(std::strcmp(text, "192.168.4.1") == 0);
    assert(brick::core::network::format_ipv4({ { 255, 255, 255, 255 } }, text, sizeof(text)) == Status::ok);
    assert(std::strcmp(text, "255.255.255.255") == 0);
    char small[8];
    assert(brick::core::network::format_ipv4({}, small, sizeof(small)) == Status::no_memory);

    Ipv4Address address;
    assert(brick::core::network::parse_ipv4("10.0.20.254", address));
    assert(address == (Ipv4Address{ { 10, 0, 20, 254 } }));
    assert(!brick::core::network::parse_ipv4("10.0.20", address));
    assert(!brick::core::network::parse_ipv4("10.0.20.256", address));
    assert(!brick::core::network::parse_ipv4("10.0.20.1.5", address));
    assert(!brick::core::network::parse_ipv4("10.0.020a.1", address));
    assert(!brick::core::network::parse_ipv4("example.com", address));
    assert(!brick::core::network::parse_ipv4("", address));
    assert(address == (Ipv4Address{ { 10, 0, 20, 254 } }));

    char mac[brick::core::network::kMacTextCapacity];
    assert(brick::core::network::format_mac({ { 0x24, 0x0A, 0xC4, 0x01, 0xBE, 0xEF } }, mac, sizeof(mac))
           == Status::ok);
    assert(std::strcmp(mac, "24:0A:C4:01:BE:EF") == 0);
}

void test_wifi_config_check()
{
    using brick::core::network::check_access_point;
    using brick::core::network::check_credentials;
    assert(check_credentials({ "home", "password1" }) == Status::ok);
    assert(check_credentials({ "cafe", nullptr }) == Status::ok);
    assert(check_credentials({ "cafe", "" }) == Status::ok);
    assert(check_credentials({ "home", "short" }) == Status::invalid_argument);
    assert(check_credentials({ "", "password1" }) == Status::invalid_argument);
    assert(check_credentials({ nullptr, "password1" }) == Status::invalid_argument);
    assert(check_credentials({ "123456789012345678901234567890123", "" }) == Status::invalid_argument);
    static_assert(brick::core::network::same_ssid("a", "a") && !brick::core::network::same_ssid("a", "ab"));

    WifiAccessPointConfig hotspot{ "BRICK-SETUP", "brick-setup" };
    assert(check_access_point(hotspot) == Status::ok);
    hotspot.channel = 14;
    assert(check_access_point(hotspot) == Status::invalid_argument);
    hotspot.channel     = 6;
    hotspot.max_clients = 0;
    assert(check_access_point(hotspot) == Status::invalid_argument);

    char buffer[5];
    assert(brick::core::network::copy_text(buffer, sizeof(buffer), "abcd") == Status::ok);
    assert(brick::core::network::copy_text(buffer, sizeof(buffer), "abcde") == Status::invalid_argument);
}

void test_wifi_network_choice()
{
    using brick::core::network::choose_wifi_network;
    using brick::core::network::kNoWifiNetwork;
    const WifiCredentials                         known[]   = { { "home", "password1" },
                                                                { "office", "password2" },
                                                                { "lab", "password3", true } };
    const brick::interfaces::network::WifiNetwork visible[] = { FakeWifiStation::network("office", -40),
                                                                FakeWifiStation::network("street", -45),
                                                                FakeWifiStation::network("home", -70) };
    // Strongest known network first, the one that failed last only when nothing else is in range.
    assert(choose_wifi_network(known, 3, visible, 3, kNoWifiNetwork) == 1);
    assert(choose_wifi_network(known, 3, visible, 3, 1) == 0);
    assert(choose_wifi_network(known, 3, visible + 2, 1, 0) == 0);
    // Nothing known in range: the hidden network.
    assert(choose_wifi_network(known, 3, visible + 1, 1, kNoWifiNetwork) == 2);
    assert(choose_wifi_network(known, 2, visible + 1, 1, kNoWifiNetwork) == kNoWifiNetwork);
}

void test_wifi_scan_list()
{
    brick::interfaces::network::WifiNetwork list[3];
    std::size_t                             count = 0;
    const auto                              add   = [&](const char* ssid, std::int8_t rssi_dbm)
    { brick::core::network::add_scanned_network(list, 3, count, FakeWifiStation::network(ssid, rssi_dbm)); };
    add("home", -70);
    add("office", -40);
    add("home", -55);  // a stronger access point of the same network replaces the weaker one
    add("home", -80);
    assert(count == 2 && std::strcmp(list[0].ssid, "office") == 0 && list[1].rssi_dbm == -55);
    add("", -60);
    add("", -65);  // hidden networks are kept separately
    assert(count == 3 && list[2].rssi_dbm == -60);
    add("street", -90);  // weaker than every entry of a full list
    assert(count == 3 && list[2].rssi_dbm == -60);
    add("cafe", -30);
    assert(count == 3 && std::strcmp(list[0].ssid, "cafe") == 0 && list[2].rssi_dbm == -55);
}

struct ManagerRig
{
    FakeClock           clock;
    RecordingLogger     logger;
    FakeWifiStation     station;
    FakeWifiAccessPoint hotspot;
    WifiManager         manager{ station, clock, logger, &hotspot };

    void advance(std::uint32_t milliseconds)
    {
        clock.now += milliseconds;
        manager.update();
    }
};

void test_wifi_manager_joins_strongest_known_network()
{
    ManagerRig            rig;
    const WifiCredentials known[] = { { "home", "password1" }, { "office", "password2" } };
    rig.station.visible           = { FakeWifiStation::network("office", -48), FakeWifiStation::network("home", -60) };
    WifiManagerConfig config;
    config.networks      = known;
    config.network_count = 2;
    assert(rig.manager.start(config) == Status::ok);
    assert(rig.manager.state() == WifiManagerState::scanning);
    rig.advance(10);
    assert(rig.manager.state() == WifiManagerState::connecting);
    assert(rig.station.joined_ssids.back() == "office");
    assert(rig.manager.network_count() == 2 && rig.manager.scan_count() == 1);
    rig.station.join();
    rig.advance(10);
    assert(rig.manager.state() == WifiManagerState::connected);
    assert(std::strcmp(rig.manager.current_network()->ssid, "office") == 0);
    assert(rig.manager.failed_attempts() == 0);

    // A scan for the screen keeps the connection.
    rig.station.scan_finished = false;
    assert(rig.manager.request_scan() == Status::ok);
    assert(rig.manager.request_scan() == Status::busy);
    rig.advance(10);
    rig.station.scan_finished = true;
    rig.advance(10);
    assert(rig.manager.scan_count() == 2 && rig.manager.state() == WifiManagerState::connected);
}

void test_wifi_manager_retries_and_opens_hotspot()
{
    ManagerRig                  rig;
    const WifiCredentials       known[] = { { "home", "password1" } };
    const WifiAccessPointConfig hotspot{ "BRICK-SETUP", "brick-setup" };
    rig.station.visible = { FakeWifiStation::network("home", -60) };
    WifiManagerConfig config;
    config.networks                 = known;
    config.network_count            = 1;
    config.retry_delay_ms           = 1000;
    config.max_retry_delay_ms       = 3000;
    config.fallback_access_point    = &hotspot;
    config.attempts_before_fallback = 2;
    assert(rig.manager.start(config) == Status::ok);

    rig.advance(10);
    rig.station.reject(WifiFailure::authentication_failed);
    rig.advance(10);
    assert(rig.manager.state() == WifiManagerState::waiting);
    assert(rig.manager.last_failure() == WifiFailure::authentication_failed);
    assert(!rig.manager.access_point_active());
    rig.advance(999);
    assert(rig.manager.state() == WifiManagerState::waiting);
    rig.advance(1);
    assert(rig.manager.state() == WifiManagerState::scanning);

    // Second failure: no answer in time; the hotspot opens.
    rig.advance(10);
    rig.advance(config.connect_timeout_ms);
    assert(rig.manager.last_failure() == WifiFailure::timeout && rig.manager.failed_attempts() == 2);
    assert(rig.manager.access_point_active() && rig.hotspot.ssid == "BRICK-SETUP");
    rig.advance(1999);
    assert(rig.manager.state() == WifiManagerState::waiting);  // the pause doubled
    rig.advance(1);
    rig.advance(10);
    rig.station.join();
    rig.advance(10);
    assert(rig.manager.state() == WifiManagerState::connected);
    assert(!rig.manager.access_point_active() && rig.hotspot.stops == 1);
}

void test_wifi_manager_rejoins_after_loss()
{
    ManagerRig            rig;
    const WifiCredentials known[] = { { "home", "password1" } };
    rig.station.visible           = { FakeWifiStation::network("home", -60) };
    WifiManagerConfig config;
    config.networks      = known;
    config.network_count = 1;
    assert(rig.manager.start(config) == Status::ok);
    rig.advance(10);
    rig.station.join();
    rig.advance(10);
    rig.station.reject(WifiFailure::connection_lost);
    rig.advance(10);
    assert(rig.manager.state() == WifiManagerState::waiting);
    assert(rig.manager.last_failure() == WifiFailure::connection_lost);
    rig.advance(config.retry_delay_ms);
    rig.advance(10);
    assert(rig.manager.state() == WifiManagerState::connecting && rig.station.joined_ssids.size() == 2);

    // Nothing in range: network_not_found, without a hotspot configured.
    rig.station.reject(WifiFailure::network_not_found);
    rig.advance(10);
    rig.station.visible.clear();
    rig.advance(config.max_retry_delay_ms);
    rig.advance(10);
    assert(rig.manager.last_failure() == WifiFailure::network_not_found);
    assert(!rig.manager.access_point_active() && rig.hotspot.starts == 0);

    rig.manager.stop();
    assert(rig.manager.state() == WifiManagerState::stopped);
    assert(rig.manager.request_scan() == Status::not_ready);
}

void test_wifi_manager_rejects_bad_config()
{
    ManagerRig        rig;
    WifiManagerConfig config;
    assert(rig.manager.start(config) == Status::invalid_argument);
    const WifiCredentials bad[] = { { "home", "short" } };
    config.networks             = bad;
    config.network_count        = 1;
    assert(rig.manager.start(config) == Status::invalid_argument);

    // Only a hotspot: it opens at the first scan that finds nothing.
    const WifiAccessPointConfig hotspot{ "BRICK-SETUP", nullptr };
    config.networks              = nullptr;
    config.network_count         = 0;
    config.fallback_access_point = &hotspot;
    assert(rig.manager.start(config) == Status::ok);
    rig.advance(10);
    assert(rig.manager.access_point_active());

    FakeClock       clock;
    RecordingLogger logger;
    FakeWifiStation station;
    WifiManager     without_access_point(station, clock, logger);
    assert(without_access_point.start(config) == Status::invalid_argument);
}

void test_network_selector()
{
    brick::mocks::FakeNetworkInterface                   ethernet("ethernet");
    brick::mocks::FakeNetworkInterface                   wifi("wifi");
    brick::interfaces::network::INetworkInterface* const links[] = { &ethernet, &wifi };
    brick::core::network::NetworkSelector                network(links, 2);

    assert(network.update() == Status::ok && network.active() == nullptr);
    assert(network.link_state() == LinkState::down && std::strcmp(network.name(), "none") == 0);
    brick::interfaces::network::IpInfo info;
    assert(network.ip_info(info) == Status::not_ready);

    wifi.state = LinkState::connecting;
    assert(network.update() == Status::ok && network.link_state() == LinkState::connecting);
    wifi.state      = LinkState::up;
    wifi.ip.address = { { 192, 168, 1, 20 } };
    assert(network.update() == Status::ok && network.active() == &wifi && wifi.default_route_calls == 1);
    assert(network.ip_info(info) == Status::ok && info.address == wifi.ip.address);
    ethernet.state = LinkState::up;
    assert(network.update() == Status::ok && network.active() == &ethernet && ethernet.default_route_calls == 1);
    assert(std::strcmp(network.name(), "ethernet") == 0);
    assert(network.update() == Status::ok && ethernet.default_route_calls == 1);
    ethernet.state = LinkState::down;
    assert(network.update() == Status::ok && network.active() == &wifi && wifi.default_route_calls == 2);
}

struct ProbeRig
{
    FakeClock                       clock;
    brick::mocks::FakeDnsResolver   dns;
    brick::mocks::FakeTcpSocket     socket;
    brick::core::network::HttpProbe probe{ dns, socket, clock };

    void run(int updates)
    {
        for (int index = 0; index < updates && probe.running(); ++index)
        {
            clock.now += 5;
            probe.update();
        }
    }
};

void test_http_probe_reads_status()
{
    ProbeRig rig;
    rig.dns.hosts["connectivitycheck.gstatic.com"] = { { 142, 250, 74, 3 } };
    rig.dns.busy_polls                             = 2;
    rig.socket.connect_polls                       = 3;
    rig.socket.send_limit                          = 20;
    rig.socket.chunk_size                          = 5;
    rig.socket.response                            = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    assert(rig.probe.start("connectivitycheck.gstatic.com", "/generate_204") == Status::ok);
    assert(rig.probe.result() == Status::busy);
    rig.run(100);
    assert(rig.probe.state() == brick::core::network::HttpProbeState::done);
    assert(rig.probe.result() == Status::ok && rig.probe.http_status() == 204);
    assert(rig.socket.remote.port == 80 && rig.socket.remote.address == rig.probe.address());
    assert(rig.socket.sent.rfind("GET /generate_204 HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\n", 0) == 0);
    assert(rig.socket.sent.size() > 60 && rig.socket.sent.substr(rig.socket.sent.size() - 4) == "\r\n\r\n");
    const auto& timings = rig.probe.timings();
    assert(timings.resolved_ms > 0 && timings.connected_ms > timings.resolved_ms);
    assert(timings.total_ms >= timings.connected_ms && rig.socket.closes > 0);
}

void test_http_probe_failures()
{
    ProbeRig rig;
    // Unknown host.
    assert(rig.probe.start("nowhere.invalid", "/") == Status::ok);
    rig.run(10);
    assert(rig.probe.result() == Status::not_found);

    // Refused connection.
    rig.dns.hosts["example.com"] = { { 93, 184, 215, 14 } };
    rig.socket.connect_result    = Status::error;
    assert(rig.probe.start("example.com", "/") == Status::ok);
    rig.run(10);
    assert(rig.probe.result() == Status::error);

    // Not an HTTP answer.
    rig.socket.connect_result = Status::ok;
    rig.socket.response       = "SSH-2.0-OpenSSH\r\n";
    assert(rig.probe.start("example.com", "/", 22) == Status::ok);
    rig.run(10);
    assert(rig.probe.result() == Status::error && rig.probe.http_status() == 0);

    // No answer in time.
    rig.socket.connect_polls = 1000;
    assert(rig.probe.start("example.com", "/", 80, 100) == Status::ok);
    rig.run(1000);
    assert(rig.probe.result() == Status::timeout);

    // Arguments that do not fit.
    char long_path[300];
    std::memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    assert(rig.probe.start("example.com", long_path) == Status::invalid_argument);
    assert(rig.probe.start("", "/") == Status::invalid_argument);

    // Cancel while running.
    assert(rig.probe.start("example.com", "/") == Status::ok);
    rig.probe.cancel();
    assert(!rig.probe.running() && rig.probe.result() == Status::not_ready);
}

}  // namespace

int main()
{
    test_ipv4_text();
    test_wifi_config_check();
    test_wifi_network_choice();
    test_wifi_scan_list();
    test_wifi_manager_joins_strongest_known_network();
    test_wifi_manager_retries_and_opens_hotspot();
    test_wifi_manager_rejoins_after_loss();
    test_wifi_manager_rejects_bad_config();
    test_network_selector();
    test_http_probe_reads_status();
    test_http_probe_failures();
    std::puts("brick_network_tests: all passed");
    return 0;
}
