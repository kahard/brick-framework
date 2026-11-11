#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/IUdpSocket.h"

namespace brick::platform::esp32
{

/// IUdpSocket over a non-blocking lwIP socket. Use it from one task.
class EspUdpSocket final : public interfaces::network::IUdpSocket
{
public:
    EspUdpSocket() = default;
    ~EspUdpSocket() override { close(); }
    EspUdpSocket(const EspUdpSocket&)            = delete;
    EspUdpSocket& operator=(const EspUdpSocket&) = delete;

    Status open(std::uint16_t port) override;
    Status receive_from(std::uint8_t* data, std::size_t capacity, std::size_t& received,
                        interfaces::network::Endpoint& from) override;
    Status send_to(const interfaces::network::Endpoint& to, const std::uint8_t* data, std::size_t size) override;
    void   close() override;

private:
    int socket_ = -1;
};

}  // namespace brick::platform::esp32
