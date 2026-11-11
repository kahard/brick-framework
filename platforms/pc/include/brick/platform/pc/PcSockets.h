#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "brick/interfaces/network/IDnsResolver.h"
#include "brick/interfaces/network/INetworkInterface.h"
#include "brick/interfaces/network/ITcpListener.h"
#include "brick/interfaces/network/ITcpSocket.h"
#include "brick/interfaces/network/IUdpSocket.h"

// The PC's real network behind the BRICK network interfaces. With offline
// set (simulator option --offline) nothing leaves the PC: the host network
// is down, lookups find nothing and connections fail, which keeps headless
// tests independent of the internet.
namespace brick::platform::pc
{

/// The PC's own connection, presented as a wired port (e.g. "ethernet"): up
/// when the PC has an IPv4 route to the internet.
class PcHostNetwork final : public brick::interfaces::network::INetworkInterface
{
public:
    PcHostNetwork(const char* name, bool offline) : name_(name), offline_(offline) {}

    /// Finds the PC's address; the interface stays down without a route.
    Status begin();

    const char*                           name() const override { return name_; }
    brick::interfaces::network::LinkState link_state() const override { return state_; }
    Status                                ip_info(brick::interfaces::network::IpInfo& info) const override;
    Status                                mac_address(brick::interfaces::network::MacAddress& mac) const override;
    Status                                set_default_route() override;

private:
    const char*                           name_;
    bool                                  offline_;
    brick::interfaces::network::LinkState state_ = brick::interfaces::network::LinkState::down;
    brick::interfaces::network::IpInfo    info_;
};

/// IDnsResolver over getaddrinfo() on a worker thread.
class PcDnsResolver final : public brick::interfaces::network::IDnsResolver
{
public:
    explicit PcDnsResolver(bool offline = false);
    ~PcDnsResolver() override;

    Status resolve(const char* host_name) override;
    Status result(brick::interfaces::network::Ipv4Address& address) override;
    void   cancel() override;

private:
    struct Lookup;
    bool                    offline_;
    std::shared_ptr<Lookup> lookup_;  ///< shared with the worker thread
};

/// ITcpSocket over a non-blocking socket (Winsock on Windows).
class PcTcpSocket final : public brick::interfaces::network::ITcpSocket
{
public:
    explicit PcTcpSocket(bool offline = false) : offline_(offline) {}
    ~PcTcpSocket() override { close(); }
    PcTcpSocket(const PcTcpSocket&)            = delete;
    PcTcpSocket& operator=(const PcTcpSocket&) = delete;

    Status connect(const brick::interfaces::network::Endpoint& remote) override;
    Status poll_connect() override;
    Status send(const std::uint8_t* data, std::size_t size, std::size_t& sent) override;
    Status receive(std::uint8_t* data, std::size_t capacity, std::size_t& received) override;
    void   close() override;

    /// Takes over a connected socket a listener accepted (PcTcpListener).
    void adopt(std::intptr_t socket);
    /// Closed and holding no socket: free for a listener.
    bool idle() const { return state_ == State::closed && socket_ == -1; }

private:
    enum class State : std::uint8_t
    {
        closed,
        connecting,
        connected,
        peer_closed,
        failed,
    };

    Status fail_(Status status);

    bool          offline_;
    std::intptr_t socket_  = -1;
    State         state_   = State::closed;
    Status        failure_ = Status::error;
};

/// ITcpListener over a non-blocking socket. It listens on this PC only
/// (127.0.0.1), or on every interface with lan set, so a simulated device's
/// pages open in a browser on http://localhost:<port>/. With port set it
/// listens there whatever port the device asks for (its port 80 is taken or
/// privileged on a PC).
class PcTcpListener final : public brick::interfaces::network::ITcpListener
{
public:
    static constexpr std::size_t kMaxConnections = 2;

    explicit PcTcpListener(bool lan = false, std::uint16_t port = 0) : lan_(lan), port_(port) {}
    ~PcTcpListener() override { close(); }
    PcTcpListener(const PcTcpListener&)            = delete;
    PcTcpListener& operator=(const PcTcpListener&) = delete;

    Status                                  listen(std::uint16_t port) override;
    brick::interfaces::network::ITcpSocket* accept() override;
    void                                    close() override;

private:
    bool                                     lan_;
    std::uint16_t                            port_;
    std::intptr_t                            socket_ = -1;
    std::array<PcTcpSocket, kMaxConnections> clients_;
};

/// IUdpSocket over a non-blocking socket, on 127.0.0.1 only (lan: on every
/// interface).
class PcUdpSocket final : public brick::interfaces::network::IUdpSocket
{
public:
    explicit PcUdpSocket(bool lan = false) : lan_(lan) {}
    ~PcUdpSocket() override { close(); }
    PcUdpSocket(const PcUdpSocket&)            = delete;
    PcUdpSocket& operator=(const PcUdpSocket&) = delete;

    Status open(std::uint16_t port) override;
    Status receive_from(std::uint8_t* data, std::size_t capacity, std::size_t& received,
                        brick::interfaces::network::Endpoint& from) override;
    Status send_to(const brick::interfaces::network::Endpoint& to, const std::uint8_t* data, std::size_t size) override;
    void   close() override;

private:
    bool          lan_;
    std::intptr_t socket_ = -1;
};

}  // namespace brick::platform::pc
