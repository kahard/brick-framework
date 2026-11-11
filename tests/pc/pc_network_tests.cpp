// Tests of the PC network platform: the simulated Wi-Fi in manual time, and
// the HTTP probe over real sockets against a server on the loopback
// interface (no internet needed).
#include <cassert>
#include <chrono>
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

/// A listener, a connection from a client socket and UDP, all on 127.0.0.1.
void test_listener_and_udp_over_loopback()
{
    using brick::platform::pc::PcTcpListener;
    using brick::platform::pc::PcTcpSocket;
    using brick::platform::pc::PcUdpSocket;
    const brick::interfaces::network::Ipv4Address loopback{ { 127, 0, 0, 1 } };
    const auto wait = [] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); };

    PcTcpListener listener;
    std::uint16_t port = 0;
    for (std::uint16_t candidate = 18731; candidate < 18760 && port == 0; ++candidate)
        if (listener.listen(candidate) == Status::ok)
            port = candidate;
    assert(port != 0 && listener.accept() == nullptr);

    PcTcpSocket client;
    assert(client.connect({ loopback, port }) == Status::ok);
    brick::interfaces::network::ITcpSocket* server = nullptr;
    for (int attempt = 0; attempt < 200 && (server == nullptr || client.poll_connect() == Status::busy); ++attempt)
    {
        if (server == nullptr)
            server = listener.accept();
        wait();
    }
    assert(server != nullptr && client.poll_connect() == Status::ok);
    std::size_t count = 0;
    assert(client.send(reinterpret_cast<const std::uint8_t*>("ping"), 4, count) == Status::ok && count == 4);
    std::uint8_t buffer[16];
    Status       status = Status::no_data;
    for (int attempt = 0; attempt < 200 && status == Status::no_data; ++attempt, wait())
        status = server->receive(buffer, sizeof(buffer), count);
    assert(status == Status::ok && count == 4 && std::memcmp(buffer, "ping", 4) == 0);
    server->close();
    for (int attempt = 0; attempt < 200 && status != Status::not_ready; ++attempt, wait())
        status = client.receive(buffer, sizeof(buffer), count);
    assert(status == Status::not_ready);  // the server closed
    listener.close();

    PcUdpSocket   receiver;
    PcUdpSocket   sender;
    std::uint16_t udp_port = 0;
    for (std::uint16_t candidate = 18761; candidate < 18790 && udp_port == 0; ++candidate)
        if (receiver.open(candidate) == Status::ok)
            udp_port = candidate;
    assert(udp_port != 0 && sender.open(static_cast<std::uint16_t>(udp_port + 1)) == Status::ok);
    brick::interfaces::network::Endpoint from;
    assert(receiver.receive_from(buffer, sizeof(buffer), count, from) == Status::no_data);
    assert(sender.send_to({ loopback, udp_port }, reinterpret_cast<const std::uint8_t*>("dns?"), 4) == Status::ok);
    status = Status::no_data;
    for (int attempt = 0; attempt < 200 && status == Status::no_data; ++attempt, wait())
        status = receiver.receive_from(buffer, sizeof(buffer), count, from);
    assert(status == Status::ok && count == 4 && from.port == udp_port + 1 && from.address == loopback);
}

}  // namespace

int main()
{
    test_listener_and_udp_over_loopback();
    test_simulated_wifi();
    test_offline_network();
    test_http_probe_over_loopback();
    std::puts("brick_pc_network_tests: all passed");
    return 0;
}
