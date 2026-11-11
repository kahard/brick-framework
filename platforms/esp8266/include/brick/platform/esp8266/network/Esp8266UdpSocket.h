#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/IUdpSocket.h"

struct udp_pcb;
struct pbuf;

namespace brick::platform::esp8266
{

/// IUdpSocket over the raw lwIP UDP API: lwIP calls back with each datagram,
/// which waits (up to kMaxWaiting; later ones are dropped) until
/// receive_from() copies it.
class Esp8266UdpSocket final : public brick::interfaces::network::IUdpSocket
{
public:
    static constexpr std::size_t kMaxWaiting = 4;

    Esp8266UdpSocket() = default;
    ~Esp8266UdpSocket() override { close(); }
    Esp8266UdpSocket(const Esp8266UdpSocket&)            = delete;
    Esp8266UdpSocket& operator=(const Esp8266UdpSocket&) = delete;

    Status open(std::uint16_t port) override;
    Status receive_from(std::uint8_t* data, std::size_t capacity, std::size_t& received,
                        brick::interfaces::network::Endpoint& from) override;
    Status send_to(const brick::interfaces::network::Endpoint& to, const std::uint8_t* data, std::size_t size) override;
    void   close() override;

    /// Called by the lwIP receive callback; takes data over.
    void on_received(pbuf* data, const brick::interfaces::network::Endpoint& from);

private:
    struct Datagram
    {
        pbuf*                                data = nullptr;
        brick::interfaces::network::Endpoint from;
    };

    udp_pcb*                          pcb_ = nullptr;
    std::array<Datagram, kMaxWaiting> waiting_{};
    std::size_t                       first_ = 0;
    std::size_t                       count_ = 0;
};

}  // namespace brick::platform::esp8266
