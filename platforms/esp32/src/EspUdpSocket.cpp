#include "brick/platform/esp32/network/EspUdpSocket.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "brick/platform/esp32/network/EspNetworkStack.h"

namespace brick::platform::esp32
{

Status EspUdpSocket::open(std::uint16_t port)
{
    close();
    if (const Status status = start_network_stack(); !is_ok(status))
        return status;
    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ < 0)
        return Status::no_memory;
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0)
    {
        const Status status = errno == EADDRINUSE ? Status::busy : Status::error;
        close();
        return status;
    }
    const int flags = ::fcntl(socket_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        close();
        return Status::error;
    }
    return Status::ok;
}

Status EspUdpSocket::receive_from(std::uint8_t* data, std::size_t capacity, std::size_t& received,
                                  interfaces::network::Endpoint& from)
{
    received = 0;
    if (socket_ < 0)
        return Status::not_ready;
    if (capacity == 0)
        return Status::invalid_argument;
    sockaddr_in   sender{};
    socklen_t     length = sizeof(sender);
    const ssize_t bytes =
        ::recvfrom(socket_, data, capacity, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&sender), &length);
    if (bytes < 0)
        return errno == EAGAIN || errno == EWOULDBLOCK ? Status::no_data : Status::error;
    received = static_cast<std::size_t>(bytes);
    std::memcpy(from.address.octets.data(), &sender.sin_addr.s_addr, from.address.octets.size());
    from.port = ntohs(sender.sin_port);
    return Status::ok;
}

Status EspUdpSocket::send_to(const interfaces::network::Endpoint& to, const std::uint8_t* data, std::size_t size)
{
    if (socket_ < 0)
        return Status::not_ready;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port   = htons(to.port);
    std::memcpy(&address.sin_addr.s_addr, to.address.octets.data(), to.address.octets.size());
    const ssize_t sent =
        ::sendto(socket_, data, size, MSG_DONTWAIT, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (sent >= 0)
        return Status::ok;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOMEM ? Status::busy : Status::error;
}

void EspUdpSocket::close()
{
    if (socket_ >= 0)
        ::close(socket_);
    socket_ = -1;
}

}  // namespace brick::platform::esp32
