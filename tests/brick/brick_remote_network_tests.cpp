// Tests of the remote network: RemoteNetworkHost and RemoteNetworkDevice over
// an in-memory serial line that can damage bytes, with the device's network
// made of the fakes in brick/mocks/NetworkFakes.h.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "brick/core/network/RemoteNetworkDevice.h"
#include "brick/core/network/RemoteNetworkHost.h"
#include "brick/mocks/Fakes.h"
#include "brick/mocks/NetworkFakes.h"

namespace
{

using brick::Status;
using brick::core::network::RemoteNetworkDevice;
using brick::core::network::RemoteNetworkDeviceConfig;
using brick::core::network::RemoteNetworkHost;
using brick::core::network::RemoteNetworkHostConfig;
using brick::interfaces::network::Endpoint;
using brick::interfaces::network::Ipv4Address;
using brick::interfaces::network::LinkState;
using brick::mocks::FakeClock;

/// One direction of the line; every corrupt_every-th byte is damaged.
struct Wire
{
    std::deque<std::uint8_t> bytes;
    std::size_t              corrupt_every = 0;
    std::size_t              count         = 0;
};

class WirePort final : public brick::interfaces::communication::ISerialPort
{
public:
    WirePort(Wire& in, Wire& out) : in_(in), out_(out) {}

    Status      begin() override { return Status::ok; }
    std::size_t read(std::uint8_t* buffer, std::size_t capacity) override
    {
        std::size_t count = 0;
        while (count < capacity && !in_.bytes.empty())
        {
            buffer[count++] = in_.bytes.front();
            in_.bytes.pop_front();
        }
        return count;
    }
    std::size_t write(const std::uint8_t* data, std::size_t length) override
    {
        const std::size_t count = length < write_limit ? length : write_limit;
        for (std::size_t index = 0; index < count; ++index)
        {
            std::uint8_t byte = data[index];
            if (out_.corrupt_every != 0 && ++out_.count % out_.corrupt_every == 0)
                byte ^= 0x5A;
            out_.bytes.push_back(byte);
        }
        return count;
    }

    std::size_t write_limit = 4096;

private:
    Wire& in_;
    Wire& out_;
};

class ArenaMemory final : public brick::interfaces::memory::IMemoryProvider
{
public:
    void* allocate(std::size_t bytes, brick::interfaces::memory::MemoryKind, std::size_t alignment) override
    {
        const std::size_t start = (used_ + alignment - 1U) / alignment * alignment;
        if (start + bytes > arena_.size())
            return nullptr;
        used_ = start + bytes;
        return arena_.data() + start;
    }
    void release(void*) override {}

private:
    std::vector<std::uint8_t> arena_ = std::vector<std::uint8_t>(1024U * 1024U);
    std::size_t               used_  = 0;
};

/// The device's program slot: keeps what it is given and checks only its size.
class FakeFirmwareUpdate final : public brick::interfaces::system::IFirmwareUpdate
{
public:
    Status begin(std::uint32_t size) override
    {
        if (size > capacity)
            return Status::no_memory;
        image.clear();
        expected = size;
        running  = true;
        ++begun;
        return Status::ok;
    }
    Status write(const std::uint8_t* data, std::size_t size, std::size_t& written) override
    {
        written = 0;
        if (!running)
            return Status::not_ready;
        image.append(reinterpret_cast<const char*>(data), size);
        written = size;
        return Status::ok;
    }
    Status finish() override
    {
        if (checks_left > 0)
        {
            --checks_left;
            return Status::busy;
        }
        running = false;
        if (image.size() != expected)
            return Status::error;
        ready = true;
        return Status::ok;
    }
    void abort() override
    {
        if (running)
            ++aborted;
        running = false;
    }
    Status restart() override
    {
        ++restarts;
        return Status::ok;
    }
    const char* description() const override { return "bench 1.0 Nov 28 2026 10:00:00"; }

    std::string   image;
    std::uint32_t capacity    = 1024U * 1024U;
    std::uint32_t expected    = 0;
    int           checks_left = 0;
    int           begun       = 0;
    int           aborted     = 0;
    int           restarts    = 0;
    bool          running     = false;
    bool          ready       = false;
};

std::string pattern(std::size_t size)
{
    std::string text(size, '\0');
    for (std::size_t index = 0; index < size; ++index)
        text[index] = static_cast<char>((index * 2654435761U) >> 24);
    return text;
}

/// Both ends with the device's fakes, on one clock.
struct Bench
{
    FakeClock                         clock;
    Wire                              to_device;
    Wire                              to_host;
    WirePort                          host_port{ to_host, to_device };
    WirePort                          device_port{ to_device, to_host };
    ArenaMemory                       host_memory;
    ArenaMemory                       device_memory;
    brick::mocks::FakeWifiStation     station;
    brick::mocks::FakeWifiAccessPoint access_point;
    brick::mocks::FakeDnsResolver     dns;
    brick::mocks::FakeTcpSocket       client0;
    brick::mocks::FakeTcpSocket       client1;
    brick::mocks::FakeTcpListener     listener;
    brick::mocks::FakeUdpSocket       udp;
    FakeFirmwareUpdate                update;
    RemoteNetworkHost                 host{ host_config() };
    RemoteNetworkDevice               device{ device_config() };

    RemoteNetworkHostConfig host_config()
    {
        RemoteNetworkHostConfig config;
        config.link.port            = &host_port;
        config.link.time            = &clock;
        config.link.memory          = &host_memory;
        config.link.window_packets  = 8;
        config.link.receive_packets = 32;
        return config;
    }

    RemoteNetworkDeviceConfig device_config()
    {
        RemoteNetworkDeviceConfig config;
        config.link.port            = &device_port;
        config.link.time            = &clock;
        config.link.memory          = &device_memory;
        config.link.window_packets  = 32;
        config.link.receive_packets = 8;
        config.station              = &station;
        config.access_point         = &access_point;
        config.dns                  = &dns;
        config.clients              = { &client0, &client1 };
        config.listener             = &listener;
        config.udp                  = &udp;
        config.update               = &update;
        return config;
    }

    void start()
    {
        assert(host.begin() == Status::ok);
        assert(device.begin() == Status::ok);
        run(20);
        assert(host.device_ready());
    }

    void run(int rounds)
    {
        for (int round = 0; round < rounds; ++round)
        {
            ++clock.now;
            device.poll();
            host.poll();
        }
    }
};

void test_handshake_and_station()
{
    Bench bench;
    bench.start();
    auto& station = bench.host.station();
    assert(station.link_state() == LinkState::down);
    brick::interfaces::network::WifiCredentials credentials;
    credentials.ssid     = "LAB";
    credentials.password = "password1";
    assert(station.connect(credentials) == Status::ok);
    assert(station.link_state() == LinkState::connecting);
    bench.run(10);
    assert(bench.station.joined_ssids.size() == 1 && bench.station.joined_ssids.back() == "LAB");
    assert(station.link_state() == LinkState::connecting);
    bench.station.join();
    bench.run(100);
    assert(station.link_state() == LinkState::up);
    brick::interfaces::network::WifiNetwork network;
    assert(station.connected_network(network) == Status::ok);
    assert(std::strcmp(network.ssid, "LAB") == 0);

    bench.station.visible = { brick::mocks::FakeWifiStation::network("A", -40),
                              brick::mocks::FakeWifiStation::network("B", -60) };
    assert(station.start_scan() == Status::ok);
    std::size_t                             count = 0;
    brick::interfaces::network::WifiNetwork found[4];
    assert(station.scan_results(found, 4, count) == Status::busy);
    bench.run(10);
    assert(station.scan_results(found, 4, count) == Status::ok);
    assert(count == 2 && std::strcmp(found[0].ssid, "A") == 0 && found[1].rssi_dbm == -60);
}

void test_dns()
{
    Bench bench;
    bench.start();
    bench.dns.hosts["camera.local"] = Ipv4Address{ { 10, 0, 0, 7 } };
    bench.dns.busy_polls            = 3;
    auto&       dns                 = bench.host.dns();
    Ipv4Address address;
    assert(dns.resolve("camera.local") == Status::ok);
    assert(dns.result(address) == Status::busy);
    bench.run(10);
    assert(dns.result(address) == Status::ok);
    assert(address == (Ipv4Address{ { 10, 0, 0, 7 } }));
    assert(dns.resolve("nowhere") == Status::ok);
    bench.run(10);
    assert(dns.result(address) == Status::not_found);
    assert(dns.resolve("192.168.1.2") == Status::ok);
    assert(dns.result(address) == Status::ok && address == (Ipv4Address{ { 192, 168, 1, 2 } }));
}

/// A stream much bigger than the host's ring, over a line that damages bytes.
void test_tcp_stream(std::size_t corrupt_every)
{
    Bench bench;
    bench.to_host.corrupt_every   = corrupt_every;
    bench.to_device.corrupt_every = corrupt_every;
    bench.start();
    const std::string expected  = pattern(300 * 1024);
    bench.client0.response      = expected;
    bench.client0.chunk_size    = 1500;
    bench.client0.connect_polls = 2;
    auto& socket                = bench.host.socket(0);
    assert(socket.connect({ { { 10, 0, 0, 7 } }, 8080 }) == Status::ok);
    assert(socket.poll_connect() == Status::busy);
    bench.run(10);
    assert(socket.poll_connect() == Status::ok);
    assert(bench.client0.remote.port == 8080);

    const char  request[] = "GET /stream HTTP/1.1\r\n\r\n";
    std::size_t sent      = 0;
    assert(socket.send(reinterpret_cast<const std::uint8_t*>(request), sizeof(request) - 1U, sent) == Status::ok);
    assert(sent == sizeof(request) - 1U);

    std::string  received;
    std::uint8_t buffer[700];
    Status       status = Status::ok;
    for (int round = 0; round < 200000 && status != Status::not_ready; ++round)
    {
        bench.run(1);
        std::size_t count = 0;
        status            = socket.receive(buffer, sizeof(buffer), count);
        received.append(reinterpret_cast<const char*>(buffer), count);
        assert(status == Status::ok || status == Status::no_data || status == Status::not_ready);
    }
    assert(status == Status::not_ready);
    assert(received.size() == expected.size());
    assert(received == expected);
    assert(bench.client0.sent == request);
    assert(bench.host.overflows() == 0);
    if (corrupt_every != 0)
        assert(bench.host.link().damaged_frames() + bench.device.link().damaged_frames() != 0);
    std::printf("  stream %u KB, damage every %u bytes: %u frames damaged, %u retransmissions\n",
                static_cast<unsigned>(expected.size() / 1024U), static_cast<unsigned>(corrupt_every),
                static_cast<unsigned>(bench.host.link().damaged_frames() + bench.device.link().damaged_frames()),
                static_cast<unsigned>(bench.host.link().retransmissions() + bench.device.link().retransmissions()));
    socket.close();
    bench.run(10);
    assert(bench.client0.closes >= 2);  // one before connecting, one now
}

void test_connect_failure()
{
    Bench bench;
    bench.start();
    bench.client1.connect_result = Status::timeout;
    auto& socket                 = bench.host.socket(1);
    assert(socket.connect({ { { 10, 0, 0, 9 } }, 80 }) == Status::ok);
    bench.run(10);
    assert(socket.poll_connect() == Status::timeout);
}

void test_listener_and_udp()
{
    Bench bench;
    bench.start();
    auto& listener = bench.host.listener();
    assert(listener.listen(80) == Status::ok);
    bench.run(5);
    assert(bench.listener.listening && bench.listener.port == 80);
    assert(listener.accept() == nullptr);

    brick::mocks::FakeTcpSocket browser;
    browser.response  = "GET / HTTP/1.1\r\n\r\n";
    browser.stay_open = true;
    bench.listener.incoming.push_back(&browser);
    bench.run(10);
    brick::interfaces::network::ITcpSocket* const connection = listener.accept();
    assert(connection != nullptr);
    assert(listener.accept() == nullptr);
    std::uint8_t buffer[64];
    std::size_t  count = 0;
    assert(connection->receive(buffer, sizeof(buffer), count) == Status::ok);
    assert(std::string(reinterpret_cast<const char*>(buffer), count) == browser.response);
    // A page longer than the device's buffer, taken by the peer 100 bytes at
    // a time, and the connection closed at once after the last byte is
    // queued: the device sends the rest before it closes.
    browser.send_limit       = 100;
    const std::string reply  = "HTTP/1.1 200 OK\r\n\r\n" + pattern(10000);
    std::size_t       queued = 0;
    for (int round = 0; round < 2000 && queued < reply.size(); ++round)
    {
        std::size_t  sent = 0;
        const Status status =
            connection->send(reinterpret_cast<const std::uint8_t*>(reply.data()) + queued, reply.size() - queued, sent);
        assert(status == Status::ok || status == Status::busy);
        queued += sent;
        bench.run(1);
    }
    assert(queued == reply.size());
    connection->close();
    bench.run(300);
    assert(browser.sent == reply);
    assert(browser.closes == 1);

    auto& udp = bench.host.udp();
    assert(udp.open(53) == Status::ok);
    bench.run(5);
    assert(bench.udp.opened && bench.udp.port == 53);
    const Endpoint phone{ { { 192, 168, 4, 2 } }, 5353 };
    bench.udp.incoming.emplace_back(phone, std::vector<std::uint8_t>{ 1, 2, 3 });
    bench.run(5);
    Endpoint from;
    assert(udp.receive_from(buffer, sizeof(buffer), count, from) == Status::ok);
    assert(count == 3 && buffer[2] == 3 && from.port == 5353 && from.address == phone.address);
    assert(udp.receive_from(buffer, sizeof(buffer), count, from) == Status::no_data);
    const std::uint8_t answer[] = { 9, 8 };
    assert(udp.send_to(phone, answer, sizeof(answer)) == Status::ok);
    bench.run(5);
    assert(bench.udp.sent.size() == 1 && bench.udp.sent[0].second.size() == 2);
}

void test_device_restart()
{
    Bench bench;
    bench.start();
    assert(bench.host.listener().listen(80) == Status::ok);
    bench.client0.stay_open = true;
    auto& socket            = bench.host.socket(0);
    assert(socket.connect({ { { 10, 0, 0, 7 } }, 8080 }) == Status::ok);
    bench.run(10);
    assert(socket.poll_connect() == Status::ok);

    // The co-processor restarts: a new device on the same line.
    bench.listener.listening = false;
    RemoteNetworkDevice restarted(bench.device_config());
    assert(restarted.begin() == Status::ok);
    for (int round = 0; round < 50; ++round)
    {
        ++bench.clock.now;
        restarted.poll();
        bench.host.poll();
    }
    assert(bench.host.device_ready());
    std::uint8_t buffer[16];
    std::size_t  count = 0;
    assert(socket.receive(buffer, sizeof(buffer), count) == Status::error);
    assert(bench.listener.listening && bench.listener.port == 80);  // listening again
}

/// A new program for the device, sent from the host; damaged bytes on the line as in test_tcp_stream().
void test_update(std::size_t corrupt_every)
{
    Bench bench;
    bench.to_device.corrupt_every = corrupt_every;
    bench.to_host.corrupt_every   = corrupt_every;
    bench.start();
    auto& update = bench.host.update();
    bench.run(20);
    assert(std::strcmp(update.description(), "bench 1.0 Nov 28 2026 10:00:00") == 0);

    const std::string image  = pattern(150000);
    bench.update.checks_left = 3;
    assert(update.finish() == Status::not_ready);
    assert(update.begin(static_cast<std::uint32_t>(image.size())) == Status::ok);
    assert(update.begin(static_cast<std::uint32_t>(image.size())) == Status::busy);
    std::size_t sent = 0;
    for (int round = 0; round < 40000 && sent < image.size(); ++round)
    {
        std::size_t written = 0;
        assert(update.write(reinterpret_cast<const std::uint8_t*>(image.data()) + sent, image.size() - sent, written)
               == Status::ok);
        sent += written;
        // The host never runs far ahead of what the device has written.
        assert(sent - bench.update.image.size() <= brick::core::network::remote::kUpdateAheadBytes);
        bench.run(1);
    }
    assert(sent == image.size());
    Status status = update.finish();
    for (int round = 0; round < 2000 && status == Status::busy; ++round)
    {
        bench.run(1);
        status = update.finish();
    }
    assert(status == Status::ok);
    assert(update.written() == image.size());
    assert(bench.update.ready && bench.update.image == image);
    assert(bench.update.restarts == 0);
    assert(update.restart() == Status::ok);
    bench.run(400);
    assert(bench.update.restarts == 1);
}

void test_update_refused_and_given_up()
{
    Bench bench;
    bench.start();
    auto& update          = bench.host.update();
    bench.update.capacity = 1000;
    assert(update.begin(5000) == Status::ok);  // the device has not answered yet
    bench.run(20);
    std::uint8_t bytes[100]{};
    std::size_t  written = 1;
    assert(update.write(bytes, sizeof(bytes), written) == Status::no_memory && written == 0);
    assert(update.finish() == Status::no_memory);

    // The next one is given up half way: the device drops what it has.
    bench.update.capacity = 100000;
    assert(update.begin(5000) == Status::ok);
    bench.run(20);
    assert(update.write(bytes, sizeof(bytes), written) == Status::ok && written == sizeof(bytes));
    bench.run(20);
    assert(bench.update.image.size() == sizeof(bytes) && bench.update.running);
    update.abort();
    bench.run(20);
    assert(bench.update.aborted == 1 && !bench.update.running);
    assert(update.write(bytes, sizeof(bytes), written) == Status::not_ready);

    // More than the image has is refused here; the device ends an update that is cut short.
    assert(update.begin(150) == Status::ok);
    bench.run(20);
    assert(update.write(bytes, sizeof(bytes), written) == Status::ok && written == sizeof(bytes));
    assert(update.write(bytes, sizeof(bytes), written) == Status::invalid_argument);
    assert(update.finish() == Status::invalid_argument);
    assert(bench.update.begun == 2);
}

void test_update_of_a_device_without_it()
{
    Bench bench;
    bench.start();
    auto config   = bench.device_config();
    config.update = nullptr;
    RemoteNetworkDevice plain(config);  // the device restarts with a program that takes none
    assert(plain.begin() == Status::ok);
    auto run = [&](int rounds)
    {
        for (int round = 0; round < rounds; ++round)
        {
            ++bench.clock.now;
            plain.poll();
            bench.host.poll();
        }
    };
    run(50);
    assert(bench.host.device_ready());
    auto& update = bench.host.update();
    assert(update.description()[0] == '\0');
    assert(update.begin(1000) == Status::ok);
    run(20);
    std::uint8_t bytes[10]{};
    std::size_t  written = 0;
    assert(update.write(bytes, sizeof(bytes), written) == Status::not_supported);
    assert(update.restart() == Status::ok);  // asked; such a device stays as it is
    run(400);
    assert(bench.host.device_ready());
}

void test_update_cut_by_a_restart()
{
    Bench bench;
    bench.start();
    auto& update = bench.host.update();
    assert(update.begin(5000) == Status::ok);
    bench.run(20);
    std::uint8_t bytes[100]{};
    std::size_t  written = 0;
    assert(update.write(bytes, sizeof(bytes), written) == Status::ok && written == sizeof(bytes));
    bench.run(20);

    RemoteNetworkDevice restarted(bench.device_config());
    assert(restarted.begin() == Status::ok);
    for (int round = 0; round < 50; ++round)
    {
        ++bench.clock.now;
        restarted.poll();
        bench.host.poll();
    }
    assert(bench.host.device_ready());
    assert(update.write(bytes, sizeof(bytes), written) == Status::error);
    assert(update.finish() == Status::error);
    assert(update.begin(5000) == Status::ok);  // and a new one may start
}

}  // namespace

int main()
{
    test_handshake_and_station();
    test_dns();
    test_tcp_stream(0);
    test_tcp_stream(20011);
    test_tcp_stream(3001);
    test_connect_failure();
    test_listener_and_udp();
    test_device_restart();
    test_update(0);
    test_update(20011);
    test_update(3001);
    test_update_refused_and_given_up();
    test_update_of_a_device_without_it();
    test_update_cut_by_a_restart();
    std::puts("brick_remote_network_tests passed");
    return 0;
}
