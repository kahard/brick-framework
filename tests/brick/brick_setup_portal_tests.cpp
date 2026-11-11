// Tests of the Wi-Fi setup page and its parts: the HTTP server, form
// decoding, the captive DNS answer, the credential store and the portal
// itself, against the fakes in brick/mocks/NetworkFakes.h.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "brick/core/network/CaptiveDns.h"
#include "brick/core/network/HttpServer.h"
#include "brick/core/network/WifiCredentialStore.h"
#include "brick/core/network/WifiManager.h"
#include "brick/core/network/WifiSetupPortal.h"
#include "brick/interfaces/storage/IByteStorage.h"
#include "brick/mocks/Fakes.h"
#include "brick/mocks/NetworkFakes.h"

namespace
{

using brick::Status;
using brick::core::network::HttpMethod;
using brick::core::network::HttpRequest;
using brick::core::network::HttpResponse;
using brick::core::network::HttpServer;
using brick::core::network::WifiCredentialStore;
using brick::core::network::WifiManager;
using brick::core::network::WifiManagerConfig;
using brick::core::network::WifiManagerState;
using brick::interfaces::network::Ipv4Address;
using brick::interfaces::network::WifiAccessPointConfig;
using brick::interfaces::network::WifiCredentials;
using brick::mocks::FakeClock;
using brick::mocks::FakeTcpListener;
using brick::mocks::FakeTcpSocket;
using brick::mocks::FakeUdpSocket;
using brick::mocks::FakeWifiAccessPoint;
using brick::mocks::FakeWifiStation;
using brick::mocks::RecordingLogger;

bool starts_with(const std::string& text, const char* prefix)
{
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

void test_http_form_value()
{
    using brick::core::network::http_form_value;
    char value[16];
    assert(http_form_value("ssid=My+Net&password=a%26b%3D", "password", value, sizeof(value)));
    assert(std::strcmp(value, "a&b=") == 0);
    assert(http_form_value("ssid=My+Net&password=a%26b%3D", "ssid", value, sizeof(value)));
    assert(std::strcmp(value, "My Net") == 0);
    assert(http_form_value("ssid=&other=x", "ssid", value, sizeof(value)) && value[0] == '\0');
    assert(!http_form_value("assid=x", "ssid", value, sizeof(value)));
    assert(!http_form_value("ssid=0123456789abcdefXYZ", "ssid", value, sizeof(value)) && value[0] == '\0');
    assert(http_form_value("a=%zz%4", "a", value, sizeof(value)) && std::strcmp(value, "%zz%4") == 0);
}

void test_http_response()
{
    char         buffer[64];
    HttpResponse response(buffer, sizeof(buffer));
    response.start(200, "text/plain");
    assert(starts_with(response.data(), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"));
    char         small[12];
    HttpResponse cut(small, sizeof(small));
    cut.escaped("<a href=\"x\">").number(-42);
    assert(cut.truncated() && std::strcmp(cut.data(), "&lt;a href=") == 0);
    HttpResponse digits(small, sizeof(small));
    digits.number(-1205).text(" ").number(0);
    assert(std::strcmp(digits.data(), "-1205 0") == 0);
}

/// Handler that records the request and answers with its path.
class EchoHandler final : public brick::core::network::IHttpHandler
{
public:
    HttpMethod  method = HttpMethod::other;
    std::string path, query, host, body;
    int         calls = 0;

    void handle(const HttpRequest& request, HttpResponse& response) override
    {
        ++calls;
        method = request.method;
        path   = request.path;
        query  = request.query;
        host   = request.host;
        body.assign(request.body, request.body_size);
        response.start(200, "text/plain").text("path=").text(request.path);
    }
};

struct ServerRig
{
    FakeClock       clock;
    RecordingLogger logger;
    FakeTcpListener listener;
    EchoHandler     handler;
    char            request[256]{};
    char            response[256]{};
    HttpServer      server{ listener, clock, logger, handler, request, sizeof(request), response, sizeof(response) };

    void serve(FakeTcpSocket& client, int updates = 40)
    {
        listener.incoming.push_back(&client);
        for (int index = 0; index < updates; ++index)
        {
            clock.now += 1;
            server.update();
        }
    }
};

void test_http_server_answers_get_and_post()
{
    ServerRig rig;
    assert(rig.server.start(8080) == Status::ok && rig.listener.port == 8080);

    FakeTcpSocket get;
    get.response   = "GET /scan?x=1 HTTP/1.1\r\nHost: 192.168.4.1:80\r\nAccept: */*\r\n\r\n";
    get.chunk_size = 7;
    get.stay_open  = true;
    get.send_limit = 10;  // the answer goes out over several update() calls
    rig.serve(get);
    assert(rig.handler.calls == 1 && rig.handler.method == HttpMethod::get);
    assert(rig.handler.path == "/scan" && rig.handler.query == "x=1" && rig.handler.host == "192.168.4.1");
    assert(starts_with(get.sent, "HTTP/1.1 200 OK\r\n") && contains(get.sent, "\r\n\r\npath=/scan"));
    assert(get.closes == 1 && !rig.server.busy());

    FakeTcpSocket post;
    post.response   = "POST /connect HTTP/1.1\r\ncontent-length: 21\r\n\r\nssid=home&password=x";
    post.chunk_size = 5;
    post.stay_open  = true;
    rig.serve(post);
    // One byte short: the server waits for the rest, then gives up.
    assert(rig.handler.calls == 1 && post.sent.empty());
    rig.clock.now += HttpServer::kRequestTimeoutMs;
    rig.server.update();
    rig.server.update();
    assert(starts_with(post.sent, "HTTP/1.1 408 ") && post.closes == 1);

    FakeTcpSocket full;
    full.response  = "POST /connect HTTP/1.1\r\nContent-Length: 20\r\n\r\nssid=home&password=x";
    full.stay_open = true;
    rig.serve(full);
    assert(rig.handler.calls == 2 && rig.handler.method == HttpMethod::post);
    assert(rig.handler.body == "ssid=home&password=x" && rig.server.served() == 2);
}

void test_http_server_rejects_large_requests()
{
    ServerRig     rig;
    FakeTcpSocket body;
    body.response  = "POST / HTTP/1.1\r\nContent-Length: 999\r\n\r\nx";
    body.stay_open = true;
    assert(rig.server.start(80) == Status::ok);
    rig.serve(body);
    assert(starts_with(body.sent, "HTTP/1.1 413 ") && rig.handler.calls == 0);

    FakeTcpSocket headers;
    headers.response   = "GET / HTTP/1.1\r\nCookie: " + std::string(400, 'c');
    headers.chunk_size = 64;
    headers.stay_open  = true;
    rig.serve(headers);
    assert(starts_with(headers.sent, "HTTP/1.1 431 ") && rig.handler.calls == 0);

    // A client that closes early is dropped without an answer.
    FakeTcpSocket gone;
    gone.response = "GET / HTTP/1.1\r\n";
    rig.serve(gone);
    assert(gone.sent.empty() && gone.closes == 1 && !rig.server.busy());
}

std::vector<std::uint8_t> dns_query(const char* name, std::uint16_t type, bool edns)
{
    std::vector<std::uint8_t> query = { 0x12, 0x34, 0x01, 0x00, 0, 1,
                                        0,    0,    0,    0,    0, static_cast<std::uint8_t>(edns ? 1 : 0) };
    for (const char* label = name; *label != '\0';)
    {
        const char*       dot    = std::strchr(label, '.');
        const std::size_t length = dot == nullptr ? std::strlen(label) : static_cast<std::size_t>(dot - label);
        query.push_back(static_cast<std::uint8_t>(length));
        query.insert(query.end(), label, label + length);
        label += length + (dot == nullptr ? 0 : 1);
    }
    query.insert(query.end(), { 0, 0, static_cast<std::uint8_t>(type), 0, 1 });
    if (edns)
        query.insert(query.end(), { 0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0 });
    return query;
}

void test_captive_dns_answer()
{
    using brick::core::network::captive_dns_answer;
    const Ipv4Address device{ { 192, 168, 4, 1 } };

    std::vector<std::uint8_t> message = dns_query("connectivitycheck.gstatic.com", 1, true);
    const std::size_t         asked   = message.size();
    message.resize(512);
    const std::size_t size = captive_dns_answer(message.data(), asked, message.size(), device);
    assert(size == asked - 11 + 16);  // the EDNS record dropped, one answer added
    assert(message[0] == 0x12 && message[1] == 0x34);
    assert(message[2] == 0x85 && message[3] == 0x80);  // response, authoritative, recursion desired + available
    assert(message[7] == 1 && message[11] == 0);
    assert(message[size - 4] == 192 && message[size - 3] == 168 && message[size - 2] == 4 && message[size - 1] == 1);

    message                = dns_query("example.com", 28, false);  // AAAA: no address to give
    const std::size_t aaaa = message.size();
    message.resize(512);
    assert(captive_dns_answer(message.data(), aaaa, message.size(), device) == aaaa && message[7] == 0);

    std::uint8_t answer_flag[12] = { 0, 0, 0x80, 0, 0, 1 };  // a response, not a query
    assert(captive_dns_answer(answer_flag, sizeof(answer_flag), sizeof(answer_flag), device) == 0);
    std::vector<std::uint8_t> cut = dns_query("example.com", 1, false);
    assert(captive_dns_answer(cut.data(), cut.size() - 3, cut.size(), device) == 0);

    FakeUdpSocket                    socket;
    RecordingLogger                  logger;
    brick::core::network::CaptiveDns dns(socket, logger);
    assert(dns.start(device) == Status::ok && socket.port == 53);
    const brick::interfaces::network::Endpoint phone{ { { 192, 168, 4, 2 } }, 5353 };
    socket.incoming.emplace_back(phone, dns_query("example.com", 1, false));
    dns.update();
    assert(dns.answered() == 1 && socket.sent.size() == 1 && socket.sent[0].first.port == 5353);
    dns.stop();
    assert(!socket.opened);
}

/// IByteStorage in a vector, blank (0xFF) at start.
class MemoryStorage final : public brick::interfaces::storage::IByteStorage
{
public:
    std::vector<std::uint8_t> bytes  = std::vector<std::uint8_t>(256, 0xFF);
    int                       writes = 0;

    Status read(std::uint32_t address, void* data, std::size_t size) override
    {
        if (address + size > bytes.size())
            return Status::invalid_argument;
        std::memcpy(data, bytes.data() + address, size);
        return Status::ok;
    }
    Status write(std::uint32_t address, const void* data, std::size_t size) override
    {
        if (address + size > bytes.size())
            return Status::invalid_argument;
        std::memcpy(bytes.data() + address, data, size);
        ++writes;
        return Status::ok;
    }
};

void test_wifi_credential_store()
{
    MemoryStorage       storage;
    WifiCredentialStore store(storage, 16);
    assert(store.load() == Status::not_found && !store.stored() && store.network() == nullptr);
    assert(store.save({ "home", "short" }) == Status::invalid_argument && storage.writes == 0);
    assert(store.save({ "home", "password1" }) == Status::ok);
    assert(std::strcmp(store.network()->ssid, "home") == 0);

    WifiCredentialStore again(storage, 16);
    assert(again.load() == Status::ok && std::strcmp(again.network()->password, "password1") == 0);
    assert(!again.network()->hidden);
    assert(again.forget() == Status::ok && again.stored() && again.network() == nullptr);
    WifiCredentialStore forgotten(storage, 16);
    assert(forgotten.load() == Status::ok && forgotten.stored() && forgotten.network() == nullptr);

    assert(forgotten.save({ "lab", "", true }) == Status::ok);
    storage.bytes[16 + 6] ^= 0x01;  // a damaged record is not used
    WifiCredentialStore damaged(storage, 16);
    assert(damaged.load() == Status::not_found && damaged.network() == nullptr);
}

struct PortalRig
{
    FakeClock                             clock;
    RecordingLogger                       logger;
    FakeWifiStation                       station;
    FakeWifiAccessPoint                   hotspot;
    FakeTcpListener                       listener;
    FakeUdpSocket                         dns_socket;
    MemoryStorage                         storage;
    WifiCredentialStore                   store{ storage };
    WifiManager                           manager{ station, clock, logger, &hotspot };
    brick::core::network::WifiSetupPortal portal{
        manager, store, station, hotspot, listener, &dns_socket, clock, logger
    };
    const WifiAccessPointConfig setup{ "BRICK-SETUP", "brick-setup" };
    std::vector<FakeTcpSocket*> clients;

    void update(int times = 1)
    {
        for (int index = 0; index < times; ++index)
        {
            clock.now += 10;
            manager.update();
            portal.update();
        }
    }

    std::string request(const std::string& text)
    {
        auto* client      = new FakeTcpSocket;
        client->response  = text;
        client->stay_open = true;
        clients.push_back(client);
        listener.incoming.push_back(client);
        update(5);
        return client->sent;
    }

    ~PortalRig()
    {
        for (FakeTcpSocket* client : clients)
            delete client;
    }
};

void test_wifi_setup_portal()
{
    PortalRig rig;
    rig.station.visible = { FakeWifiStation::network("home", -50), FakeWifiStation::network("cafe <&>", -70) };
    WifiManagerConfig config;
    config.fallback_access_point = &rig.setup;
    assert(rig.manager.start(config) == Status::ok);
    assert(rig.portal.start() == Status::ok && rig.listener.port == 80);
    rig.update(3);
    assert(rig.manager.access_point_active() && rig.dns_socket.opened);  // the DNS follows the hotspot

    // A phone's connectivity check lands on the setup page.
    std::string answer = rig.request("GET /generate_204 HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\n\r\n");
    assert(starts_with(answer, "HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\n"));

    answer = rig.request("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n");
    assert(starts_with(answer, "HTTP/1.1 200 OK") && contains(answer, "No network set up yet"));
    assert(contains(answer, "<option value=\"home\">home (-50 dBm)</option>"));
    assert(contains(answer, "cafe &lt;&amp;&gt;") && !contains(answer, "/forget"));

    answer = rig.request("POST /connect HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 21\r\n\r\n"
                         "ssid=home&password=x1");
    assert(contains(answer, "Not saved") && !rig.store.stored());

    const char form[] = "ssid=home&other=&password=password1";
    answer = rig.request(std::string("POST /connect HTTP/1.1\r\nContent-Length: ") + std::to_string(sizeof(form) - 1)
                         + "\r\n\r\n" + form);
    assert(contains(answer, "Joining <b>home</b>") && rig.store.network() != nullptr);
    rig.update(3);
    assert(rig.manager.known_network_count() == 1 && rig.station.joined_ssids.back() == "home");
    rig.station.join();
    rig.update(3);
    assert(rig.manager.state() == WifiManagerState::connected && !rig.manager.access_point_active());
    assert(!rig.dns_socket.opened);  // the hotspot closed, so did the DNS

    answer = rig.request("GET / HTTP/1.1\r\nHost: 192.168.1.20\r\n\r\n");
    assert(contains(answer, "Connected to <b>home</b>") && contains(answer, "action=\"/forget\""));

    answer = rig.request("POST /forget HTTP/1.1\r\nHost: 192.168.1.20\r\nContent-Length: 0\r\n\r\n");
    assert(contains(answer, "Network forgotten"));
    rig.update(3);
    assert(rig.manager.known_network_count() == 0 && rig.manager.access_point_active());
    assert(rig.station.disconnects > 0 && rig.store.stored() && rig.store.network() == nullptr);

    // A hidden network is typed in.
    const char hidden[] = "ssid=home&other=lab&password=";
    answer = rig.request(std::string("POST /connect HTTP/1.1\r\nContent-Length: ") + std::to_string(sizeof(hidden) - 1)
                         + "\r\n\r\n" + hidden);
    assert(contains(answer, "Joining <b>lab</b>") && rig.store.network()->hidden);
    rig.update(3);
    assert(rig.manager.known_network_count() == 1);

    // A button held down forgets it without the page.
    assert(rig.portal.forget_network() == Status::ok);
    assert(rig.manager.known_network_count() == 0 && rig.store.stored() && rig.store.network() == nullptr);
    rig.portal.stop();
    assert(!rig.listener.listening && !rig.dns_socket.opened);
}

}  // namespace

int main()
{
    test_http_form_value();
    test_http_response();
    test_http_server_answers_get_and_post();
    test_http_server_rejects_large_requests();
    test_captive_dns_answer();
    test_wifi_credential_store();
    test_wifi_setup_portal();
    std::puts("brick_setup_portal_tests: all passed");
    return 0;
}
