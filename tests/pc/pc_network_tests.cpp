// Tests of the PC network platform: the simulated Wi-Fi in manual time, and
// the HTTP probe over real sockets against a server on the loopback
// interface (no internet needed).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

#include "brick/core/network/HttpProbe.h"
#include "brick/platform/pc/PcSockets.h"
#include "brick/platform/pc/PcTime.h"
#include "brick/platform/pc/PcWifi.h"

namespace
{

using brick::Status;
using brick::interfaces::network::LinkState;
using brick::interfaces::network::WifiFailure;
using brick::interfaces::network::WifiNetwork;
using brick::platform::pc::PcTime;
using brick::platform::pc::PcWifi;
using brick::platform::pc::PcWifiStation;

void test_simulated_wifi()
{
    PcTime time(true);
    PcWifi wifi(time, {});
    auto&  station = wifi.station();

    WifiNetwork networks[4];
    std::size_t count = 0;
    assert(station.scan_results(networks, 4, count) == Status::not_ready);
    assert(station.start_scan() == Status::ok);
    assert(station.start_scan() == Status::busy);
    assert(station.scan_results(networks, 4, count) == Status::busy);
    time.advance_ms(PcWifiStation::kScanMs);
    assert(station.scan_results(networks, 4, count) == Status::ok && count == 3);
    assert(std::strcmp(networks[0].ssid, "BRICK-LAB") == 0 && networks[0].rssi_dbm > networks[1].rssi_dbm);

    assert(station.connect({ "BRICK-LAB", "wrong-password" }) == Status::ok);
    assert(station.link_state() == LinkState::connecting);
    time.advance_ms(PcWifiStation::kJoinMs);
    assert(station.link_state() == LinkState::down && station.failure() == WifiFailure::authentication_failed);

    assert(station.connect({ "Elsewhere", "password1" }) == Status::ok);
    time.advance_ms(PcWifiStation::kNotFoundMs);
    assert(station.failure() == WifiFailure::network_not_found);

    assert(station.connect({ "BRICK-LAB", "brick-lab-pass" }) == Status::ok);
    time.advance_ms(PcWifiStation::kJoinMs);
    brick::interfaces::network::IpInfo info;
    assert(station.link_state() == LinkState::up && station.ip_info(info) == Status::ok);
    assert(station.connected_network(networks[0]) == Status::ok && std::strcmp(networks[0].ssid, "BRICK-LAB") == 0);
    assert(station.disconnect() == Status::ok && station.link_state() == LinkState::down);
    assert(station.failure() == WifiFailure::none);

    auto& hotspot = wifi.access_point();
    assert(hotspot.start({ "BRICK-SETUP", "short" }) == Status::invalid_argument);
    assert(hotspot.start({ "BRICK-SETUP", "brick-setup" }) == Status::ok);
    assert(hotspot.link_state() == LinkState::up && hotspot.ssid() == "BRICK-SETUP");
    assert(hotspot.stop() == Status::ok && hotspot.link_state() == LinkState::down);
}

#ifdef _WIN32
using Handle = SOCKET;
void close_handle(Handle handle)
{
    closesocket(handle);
}
#else
using Handle = int;
void close_handle(Handle handle)
{
    ::close(handle);
}
#endif

/// Accepts one connection, reads the request up to the empty line and
/// answers with a 204 status line.
void serve_once(Handle listener, std::string* request)
{
    const Handle client = ::accept(listener, nullptr, nullptr);
    char         buffer[512];
    while (request->find("\r\n\r\n") == std::string::npos)
    {
        const auto received = ::recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0)
            break;
        request->append(buffer, static_cast<std::size_t>(received));
    }
    const char answer[] = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    ::send(client, answer, static_cast<int>(sizeof(answer) - 1), 0);
    close_handle(client);
}

void test_http_probe_over_loopback()
{
    brick::platform::pc::PcTcpSocket   socket;
    brick::platform::pc::PcDnsResolver dns;
#ifdef _WIN32
    // The test's own server socket is created before the platform starts Winsock.
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    const Handle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in  address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    assert(::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    assert(::listen(listener, 1) == 0);
    socklen_t length = sizeof(address);
    assert(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const std::uint16_t port = ntohs(address.sin_port);

    std::string request;
    std::thread server(serve_once, listener, &request);

    PcTime                          time;
    brick::core::network::HttpProbe probe(dns, socket, time);
    assert(probe.start("127.0.0.1", "/generate_204", port, 5000) == Status::ok);
    while (probe.running())
    {
        probe.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.join();
    close_handle(listener);
    assert(probe.result() == Status::ok && probe.http_status() == 204);
    assert(request.rfind("GET /generate_204 HTTP/1.1\r\nHost: 127.0.0.1\r\n", 0) == 0);
}

void test_offline_network()
{
    brick::platform::pc::PcDnsResolver      dns(true);
    brick::interfaces::network::Ipv4Address address;
    assert(dns.result(address) == Status::not_ready);
    assert(dns.resolve("example.com") == Status::ok && dns.result(address) == Status::not_found);
    assert(dns.resolve("10.1.2.3") == Status::ok && dns.result(address) == Status::ok);
    assert(address.octets[0] == 10 && address.octets[3] == 3);

    brick::platform::pc::PcTcpSocket socket(true);
    assert(socket.connect({ address, 80 }) == Status::error && socket.poll_connect() == Status::error);

    brick::platform::pc::PcHostNetwork host("ethernet", true);
    assert(host.begin() == Status::ok && host.link_state() == LinkState::down);
}

}  // namespace

int main()
{
    test_simulated_wifi();
    test_offline_network();
    test_http_probe_over_loopback();
    std::puts("brick_pc_network_tests: all passed");
    return 0;
}
