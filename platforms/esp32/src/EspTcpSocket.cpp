#include "brick/platform/esp32/network/EspTcpSocket.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "brick/platform/esp32/network/EspNetworkStack.h"

namespace brick::platform::esp32
{

namespace
{
    Status from_errno(int error)
    {
        switch (error)
        {
            case ETIMEDOUT:
                return Status::timeout;
            case ENOMEM:
            case ENOBUFS:
            case ENFILE:
            case EMFILE:
                return Status::no_memory;
            default:
                return Status::error;
        }
    }

    bool would_block(int error)
    {
        return error == EAGAIN || error == EWOULDBLOCK;
    }
}  // namespace

Status EspTcpSocket::connect(const interfaces::network::Endpoint& remote)
{
    close();
    if (const Status status = start_network_stack(); !is_ok(status))
        return status;
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ < 0)
        return from_errno(errno);
    const int flags = ::fcntl(socket_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0)
        return fail_(Status::error);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port   = htons(remote.port);
    std::memcpy(&address.sin_addr.s_addr, remote.address.octets.data(), remote.address.octets.size());
    if (::connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
    {
        state_ = State::connected;
        return Status::ok;
    }
    if (errno != EINPROGRESS)
        return fail_(from_errno(errno));
    state_ = State::connecting;
    return Status::ok;
}

Status EspTcpSocket::poll_connect()
{
    switch (state_)
    {
        case State::closed:
            return Status::not_ready;
        case State::failed:
            return failure_;
        case State::connected:
        case State::peer_closed:
            return Status::ok;
        case State::connecting:
            break;
    }
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(socket_, &writable);
    timeval   no_wait{};
    const int ready = ::select(socket_ + 1, nullptr, &writable, nullptr, &no_wait);
    if (ready == 0)
        return Status::busy;
    int       error  = 0;
    socklen_t length = sizeof(error);
    if (ready < 0 || ::getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
        return fail_(Status::error);
    if (error != 0)
        return fail_(from_errno(error));
    state_ = State::connected;
    return Status::ok;
}

Status EspTcpSocket::send(const std::uint8_t* data, std::size_t size, std::size_t& sent)
{
    sent = 0;
    if (state_ != State::connected)
        return state_ == State::failed ? failure_ : Status::not_ready;
    if (size == 0)
        return Status::ok;
    const ssize_t written = ::send(socket_, data, size, MSG_DONTWAIT);
    if (written > 0)
    {
        sent = static_cast<std::size_t>(written);
        return Status::ok;
    }
    if (written < 0 && !would_block(errno))
        return fail_(from_errno(errno));
    return Status::busy;
}

Status EspTcpSocket::receive(std::uint8_t* data, std::size_t capacity, std::size_t& received)
{
    received = 0;
    if (state_ != State::connected)
        return state_ == State::failed ? failure_ : Status::not_ready;
    if (capacity == 0)
        return Status::invalid_argument;
    const ssize_t bytes_read = ::recv(socket_, data, capacity, MSG_DONTWAIT);
    if (bytes_read > 0)
    {
        received = static_cast<std::size_t>(bytes_read);
        return Status::ok;
    }
    if (bytes_read == 0)
    {
        state_ = State::peer_closed;
        return Status::not_ready;
    }
    return would_block(errno) ? Status::no_data : fail_(from_errno(errno));
}

void EspTcpSocket::close()
{
    if (socket_ >= 0)
        ::close(socket_);
    socket_ = -1;
    state_  = State::closed;
}

Status EspTcpSocket::fail_(Status status)
{
    if (socket_ >= 0)
        ::close(socket_);
    socket_  = -1;
    state_   = State::failed;
    failure_ = status;
    return status;
}

}  // namespace brick::platform::esp32
