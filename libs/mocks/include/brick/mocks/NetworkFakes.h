#pragma once

// Test doubles for the network interfaces. Tests script what the "network"
// does (which networks a scan finds, whether a connection succeeds, what a
// server answers) and check what the code under test asked for.
// Host-only code: may use the standard library freely.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "brick/interfaces/network/IDnsResolver.h"
#include "brick/interfaces/network/ITcpSocket.h"
#include "brick/interfaces/network/IWifiAccessPoint.h"
#include "brick/interfaces/network/IWifiStation.h"

namespace brick::mocks
{

/// Interface whose state the test sets directly.
class FakeNetworkInterface final : public brick::interfaces::network::INetworkInterface
{
public:
    explicit FakeNetworkInterface(const char* name) : name_(name) {}

    brick::interfaces::network::LinkState state = brick::interfaces::network::LinkState::down;
    brick::interfaces::network::IpInfo    ip;
    int                                   default_route_calls = 0;

    const char*                           name() const override { return name_; }
    brick::interfaces::network::LinkState link_state() const override { return state; }
    Status                                ip_info(brick::interfaces::network::IpInfo& info) const override
    {
        if (state != brick::interfaces::network::LinkState::up)
            return Status::not_ready;
        info = ip;
        return Status::ok;
    }
    Status mac_address(brick::interfaces::network::MacAddress& mac) const override
    {
        mac = {};
        return Status::ok;
    }
    Status set_default_route() override
    {
        ++default_route_calls;
        return state == brick::interfaces::network::LinkState::up ? Status::ok : Status::not_ready;
    }

private:
    const char* name_;
};

/// Station: start_scan() "finds" visible (strongest first) once
/// scan_finished is true; connect() records the SSID and waits for the test
/// to call join() or reject().
class FakeWifiStation final : public brick::interfaces::network::IWifiStation
{
public:
    std::vector<brick::interfaces::network::WifiNetwork> visible;
    bool                                                 scan_finished = true;
    Status                                               scan_status   = Status::ok;
    int                                                  scans_started = 0;
    std::vector<std::string>                             joined_ssids;
    int                                                  disconnects = 0;

    void join() { state_ = brick::interfaces::network::LinkState::up; }
    void reject(brick::interfaces::network::WifiFailure failure)
    {
        state_   = brick::interfaces::network::LinkState::down;
        failure_ = failure;
    }

    static brick::interfaces::network::WifiNetwork network(const char* ssid, std::int8_t rssi_dbm)
    {
        brick::interfaces::network::WifiNetwork result;
        std::strncpy(result.ssid, ssid, sizeof(result.ssid) - 1);
        result.rssi_dbm = rssi_dbm;
        result.security = brick::interfaces::network::WifiSecurity::wpa2;
        return result;
    }

    const char*                           name() const override { return "wifi"; }
    brick::interfaces::network::LinkState link_state() const override { return state_; }
    Status                                ip_info(brick::interfaces::network::IpInfo& info) const override
    {
        info = {};
        return state_ == brick::interfaces::network::LinkState::up ? Status::ok : Status::not_ready;
    }
    Status mac_address(brick::interfaces::network::MacAddress& mac) const override
    {
        mac = {};
        return Status::ok;
    }
    Status set_default_route() override { return Status::ok; }

    Status start_scan() override
    {
        if (scan_running_ && !scan_finished)
            return Status::busy;
        scan_running_ = true;
        scan_started_ = true;
        ++scans_started;
        return Status::ok;
    }
    Status scan_results(brick::interfaces::network::WifiNetwork* networks, std::size_t capacity,
                        std::size_t& count) override
    {
        count = 0;
        if (!scan_started_)
            return Status::not_ready;
        if (!scan_finished)
            return Status::busy;
        scan_running_ = false;
        if (scan_status != Status::ok)
            return scan_status;
        for (const auto& network : visible)
            if (count < capacity)
                networks[count++] = network;
        return Status::ok;
    }
    Status connect(const brick::interfaces::network::WifiCredentials& credentials) override
    {
        joined_ssids.emplace_back(credentials.ssid);
        state_   = brick::interfaces::network::LinkState::connecting;
        failure_ = brick::interfaces::network::WifiFailure::none;
        return Status::ok;
    }
    Status disconnect() override
    {
        ++disconnects;
        state_   = brick::interfaces::network::LinkState::down;
        failure_ = brick::interfaces::network::WifiFailure::none;
        return Status::ok;
    }
    brick::interfaces::network::WifiFailure failure() const override { return failure_; }
    Status connected_network(brick::interfaces::network::WifiNetwork& network) const override
    {
        if (state_ != brick::interfaces::network::LinkState::up || joined_ssids.empty())
            return Status::not_ready;
        network = FakeWifiStation::network(joined_ssids.back().c_str(), -50);
        return Status::ok;
    }

private:
    brick::interfaces::network::LinkState   state_        = brick::interfaces::network::LinkState::down;
    brick::interfaces::network::WifiFailure failure_      = brick::interfaces::network::WifiFailure::none;
    bool                                    scan_started_ = false;
    bool                                    scan_running_ = false;
};

/// Access point that is up between start() and stop().
class FakeWifiAccessPoint final : public brick::interfaces::network::IWifiAccessPoint
{
public:
    std::string ssid;
    int         starts  = 0;
    int         stops   = 0;
    std::size_t clients = 0;

    const char*                           name() const override { return "access point"; }
    brick::interfaces::network::LinkState link_state() const override
    {
        return running_ ? brick::interfaces::network::LinkState::up : brick::interfaces::network::LinkState::down;
    }
    Status ip_info(brick::interfaces::network::IpInfo& info) const override
    {
        info = {};
        return running_ ? Status::ok : Status::not_ready;
    }
    Status mac_address(brick::interfaces::network::MacAddress& mac) const override
    {
        mac = {};
        return Status::ok;
    }
    Status set_default_route() override { return Status::ok; }
    Status start(const brick::interfaces::network::WifiAccessPointConfig& config) override
    {
        ssid     = config.ssid;
        running_ = true;
        ++starts;
        return Status::ok;
    }
    Status stop() override
    {
        running_ = false;
        ++stops;
        return Status::ok;
    }
    std::size_t client_count() const override { return clients; }

private:
    bool running_ = false;
};

/// Resolver answering from hosts after busy_polls calls to result();
/// unknown names are Status::not_found.
class FakeDnsResolver final : public brick::interfaces::network::IDnsResolver
{
public:
    std::map<std::string, brick::interfaces::network::Ipv4Address> hosts;
    int                                                            busy_polls = 0;
    int                                                            cancels    = 0;

    Status resolve(const char* host_name) override
    {
        name_    = host_name;
        pending_ = true;
        polls_   = 0;
        return Status::ok;
    }
    Status result(brick::interfaces::network::Ipv4Address& address) override
    {
        if (!pending_)
            return Status::not_ready;
        if (polls_++ < busy_polls)
            return Status::busy;
        const auto host = hosts.find(name_);
        if (host == hosts.end())
            return Status::not_found;
        address = host->second;
        return Status::ok;
    }
    void cancel() override
    {
        pending_ = false;
        ++cancels;
    }

private:
    std::string name_;
    bool        pending_ = false;
    int         polls_   = 0;
};

/// Socket connecting after connect_polls calls to poll_connect() with
/// connect_result; it records what was sent and then delivers response in
/// chunks of chunk_size bytes, after which the server "closes".
class FakeTcpSocket final : public brick::interfaces::network::ITcpSocket
{
public:
    Status                               connect_result = Status::ok;
    int                                  connect_polls  = 0;
    std::string                          response;
    std::size_t                          chunk_size = 16;
    std::size_t                          send_limit = 1024;  ///< bytes accepted per send()
    std::string                          sent;
    brick::interfaces::network::Endpoint remote;
    int                                  closes = 0;

    Status connect(const brick::interfaces::network::Endpoint& endpoint) override
    {
        remote    = endpoint;
        state_    = State::connecting;
        polls_    = 0;
        position_ = 0;
        return Status::ok;
    }
    Status poll_connect() override
    {
        if (state_ == State::closed)
            return Status::not_ready;
        if (state_ == State::connected)
            return Status::ok;
        if (polls_++ < connect_polls)
            return Status::busy;
        state_ = connect_result == Status::ok ? State::connected : State::closed;
        return connect_result;
    }
    Status send(const std::uint8_t* data, std::size_t size, std::size_t& count) override
    {
        count = 0;
        if (state_ != State::connected)
            return Status::not_ready;
        count = size < send_limit ? size : send_limit;
        sent.append(reinterpret_cast<const char*>(data), count);
        return Status::ok;
    }
    Status receive(std::uint8_t* data, std::size_t capacity, std::size_t& received) override
    {
        received = 0;
        if (state_ != State::connected || position_ >= response.size())
            return Status::not_ready;
        std::size_t bytes = response.size() - position_;
        bytes             = bytes < chunk_size ? bytes : chunk_size;
        bytes             = bytes < capacity ? bytes : capacity;
        std::memcpy(data, response.data() + position_, bytes);
        position_ += bytes;
        received = bytes;
        return Status::ok;
    }
    void close() override
    {
        state_ = State::closed;
        ++closes;
    }

private:
    enum class State
    {
        closed,
        connecting,
        connected
    };
    State       state_    = State::closed;
    int         polls_    = 0;
    std::size_t position_ = 0;
};

}  // namespace brick::mocks
