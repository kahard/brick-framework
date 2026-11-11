#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/ITcpListener.h"
#include "brick/platform/esp32/network/EspTcpSocket.h"

namespace brick::platform::esp32
{

/// ITcpListener over a non-blocking lwIP socket; accepted connections go to
/// one of kMaxConnections sockets (with none free they wait in the backlog).
/// Use it from one task.
class EspTcpListener final : public interfaces::network::ITcpListener
{
public:
    static constexpr std::size_t kMaxConnections = 2;

    EspTcpListener() = default;
    ~EspTcpListener() override { close(); }
    EspTcpListener(const EspTcpListener&)            = delete;
    EspTcpListener& operator=(const EspTcpListener&) = delete;

    Status                           listen(std::uint16_t port) override;
    interfaces::network::ITcpSocket* accept() override;
    void                             close() override;

private:
    void stop_listening_();

    int                                       socket_ = -1;
    std::array<EspTcpSocket, kMaxConnections> sockets_;
};

}  // namespace brick::platform::esp32
