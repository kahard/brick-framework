#pragma once

#include <cstdint>

#include "brick/interfaces/network/ITcpSocket.h"

namespace brick::platform::esp32
{

/// ITcpSocket over a non-blocking lwIP socket. Use it from one task.
class EspTcpSocket final : public interfaces::network::ITcpSocket
{
public:
    EspTcpSocket() = default;
    ~EspTcpSocket() override { close(); }
    EspTcpSocket(const EspTcpSocket&)            = delete;
    EspTcpSocket& operator=(const EspTcpSocket&) = delete;

    Status connect(const interfaces::network::Endpoint& remote) override;
    Status poll_connect() override;
    Status send(const std::uint8_t* data, std::size_t size, std::size_t& sent) override;
    Status receive(std::uint8_t* data, std::size_t capacity, std::size_t& received) override;
    void   close() override;

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

    int    socket_  = -1;
    State  state_   = State::closed;
    Status failure_ = Status::error;
};

}  // namespace brick::platform::esp32
