#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/ITcpListener.h"
#include "brick/platform/esp8266/network/Esp8266TcpSocket.h"

struct tcp_pcb;

namespace brick::platform::esp8266
{

/// ITcpListener over the raw lwIP TCP API. lwIP hands each new connection to
/// a free one of kMaxConnections sockets at once; with none free the
/// connection is refused.
class Esp8266TcpListener final : public brick::interfaces::network::ITcpListener
{
public:
    static constexpr std::size_t kMaxConnections = 2;

    Esp8266TcpListener() = default;
    ~Esp8266TcpListener() override { close(); }
    Esp8266TcpListener(const Esp8266TcpListener&)            = delete;
    Esp8266TcpListener& operator=(const Esp8266TcpListener&) = delete;

    Status                                  listen(std::uint16_t port) override;
    brick::interfaces::network::ITcpSocket* accept() override;
    void                                    close() override;

    /// Called by the lwIP accept callback; false when no socket is free.
    bool on_accepted(tcp_pcb* pcb);

private:
    void stop_listening_();

    tcp_pcb*                                      listening_ = nullptr;
    std::array<Esp8266TcpSocket, kMaxConnections> sockets_;
    std::array<bool, kMaxConnections>             waiting_{};  ///< accepted, not handed out yet
};

}  // namespace brick::platform::esp8266
