#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/ITcpSocket.h"

struct tcp_pcb;
struct pbuf;

namespace brick::platform::esp8266
{

/// ITcpSocket over the raw lwIP TCP API, without blocking: lwIP calls back
/// with the connection, received data and errors; received data waits in
/// lwIP buffers until receive() copies it.
class Esp8266TcpSocket final : public brick::interfaces::network::ITcpSocket
{
public:
    Esp8266TcpSocket() = default;
    ~Esp8266TcpSocket() override { close(); }
    Esp8266TcpSocket(const Esp8266TcpSocket&)            = delete;
    Esp8266TcpSocket& operator=(const Esp8266TcpSocket&) = delete;

    Status connect(const brick::interfaces::network::Endpoint& remote) override;
    Status poll_connect() override;
    Status send(const std::uint8_t* data, std::size_t size, std::size_t& sent) override;
    Status receive(std::uint8_t* data, std::size_t capacity, std::size_t& received) override;
    void   close() override;

    // Called by the lwIP callbacks of this socket.
    void on_connected();
    void on_error(std::int8_t error);
    void on_received(pbuf* data);

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
    void   release_received_();

    tcp_pcb* pcb_      = nullptr;
    pbuf*    received_ = nullptr;  ///< chain of received, unread data
    State    state_    = State::closed;
    Status   failure_  = Status::error;
};

}  // namespace brick::platform::esp8266
