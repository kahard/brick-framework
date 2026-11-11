#include "brick/platform/esp32/network/EspTcpListener.h"

#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "brick/platform/esp32/network/EspNetworkStack.h"

namespace brick::platform::esp32
{

namespace
{
    constexpr int kBacklog = 2;
}  // namespace

Status EspTcpListener::listen(std::uint16_t port)
{
    stop_listening_();
    if (const Status status = start_network_stack(); !is_ok(status))
        return status;
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ < 0)
        return Status::no_memory;
    const int reuse = 1;
    (void)::setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    const int flags         = ::fcntl(socket_, F_GETFL, 0);
    if (::bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0)
    {
        const Status status = errno == EADDRINUSE ? Status::busy : Status::error;
        stop_listening_();
        return status;
    }
    if (flags < 0 || ::fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0 || ::listen(socket_, kBacklog) < 0)
    {
        stop_listening_();
        return Status::error;
    }
    return Status::ok;
}

interfaces::network::ITcpSocket* EspTcpListener::accept()
{
    if (socket_ < 0)
        return nullptr;
    for (EspTcpSocket& client : sockets_)
    {
        if (!client.idle())
            continue;
        const int accepted = ::accept(socket_, nullptr, nullptr);
        if (accepted < 0)
            return nullptr;
        client.adopt(accepted);
        return &client;
    }
    return nullptr;
}

void EspTcpListener::close()
{
    stop_listening_();
    for (EspTcpSocket& client : sockets_)
        client.close();
}

void EspTcpListener::stop_listening_()
{
    if (socket_ >= 0)
        ::close(socket_);
    socket_ = -1;
}

}  // namespace brick::platform::esp32
