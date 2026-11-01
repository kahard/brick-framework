#include "brick/platform/esp8266/network/Esp8266TcpSocket.h"

#include <lwip/err.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>

namespace brick::platform::esp8266
{

namespace
{
    constexpr std::size_t kMaxChunk = 0xFFFF;  ///< lwIP lengths are 16 bits

    Esp8266TcpSocket& socket_of(void* context)
    {
        return *static_cast<Esp8266TcpSocket*>(context);
    }

    err_t on_connected(void* context, tcp_pcb*, err_t)
    {
        socket_of(context).on_connected();
        return ERR_OK;
    }

    void on_error(void* context, err_t error)
    {
        socket_of(context).on_error(error);
    }

    err_t on_received(void* context, tcp_pcb*, pbuf* data, err_t error)
    {
        if (error != ERR_OK && data != nullptr)
        {
            pbuf_free(data);
            return ERR_OK;
        }
        socket_of(context).on_received(data);
        return ERR_OK;
    }
}  // namespace

Status Esp8266TcpSocket::connect(const brick::interfaces::network::Endpoint& remote)
{
    close();
    pcb_ = tcp_new();
    if (pcb_ == nullptr)
        return fail_(Status::no_memory);
    tcp_arg(pcb_, this);
    tcp_err(pcb_, brick::platform::esp8266::on_error);
    tcp_recv(pcb_, brick::platform::esp8266::on_received);
    ip_addr_t address{};
    IP_ADDR4(&address, remote.address.octets[0], remote.address.octets[1], remote.address.octets[2],
             remote.address.octets[3]);
    state_ = State::connecting;
    if (const err_t error = tcp_connect(pcb_, &address, remote.port, brick::platform::esp8266::on_connected);
        error != ERR_OK)
        return fail_(error == ERR_MEM ? Status::no_memory : Status::error);
    return Status::ok;
}

Status Esp8266TcpSocket::poll_connect()
{
    switch (state_)
    {
        case State::closed:
            return Status::not_ready;
        case State::connecting:
            return Status::busy;
        case State::failed:
            return failure_;
        case State::connected:
        case State::peer_closed:
            break;
    }
    return Status::ok;
}

Status Esp8266TcpSocket::send(const std::uint8_t* data, std::size_t size, std::size_t& sent)
{
    sent = 0;
    if (state_ != State::connected)
        return state_ == State::failed ? failure_ : Status::not_ready;
    const std::size_t room  = tcp_sndbuf(pcb_);
    const std::size_t bytes = size < room ? size : room;
    if (bytes == 0)
        return size == 0 ? Status::ok : Status::busy;
    const err_t error =
        tcp_write(pcb_, data, static_cast<u16_t>(bytes < kMaxChunk ? bytes : kMaxChunk), TCP_WRITE_FLAG_COPY);
    if (error == ERR_MEM)
        return Status::busy;
    if (error != ERR_OK)
        return fail_(Status::error);
    tcp_output(pcb_);
    sent = bytes < kMaxChunk ? bytes : kMaxChunk;
    return Status::ok;
}

Status Esp8266TcpSocket::receive(std::uint8_t* data, std::size_t capacity, std::size_t& received)
{
    received = 0;
    if (capacity == 0)
        return Status::invalid_argument;
    if (received_ != nullptr)
    {
        const std::size_t limit = capacity < kMaxChunk ? capacity : kMaxChunk;
        const u16_t       bytes = pbuf_copy_partial(received_, data, static_cast<u16_t>(limit), 0);
        received_               = pbuf_free_header(received_, bytes);
        if (pcb_ != nullptr)
            tcp_recved(pcb_, bytes);
        received = bytes;
        return Status::ok;
    }
    switch (state_)
    {
        case State::connected:
            return Status::no_data;
        case State::failed:
            return failure_;
        case State::closed:
        case State::connecting:
        case State::peer_closed:
            break;
    }
    return Status::not_ready;
}

void Esp8266TcpSocket::close()
{
    release_received_();
    if (pcb_ != nullptr)
    {
        tcp_arg(pcb_, nullptr);
        tcp_err(pcb_, nullptr);
        tcp_recv(pcb_, nullptr);
        if (tcp_close(pcb_) != ERR_OK)
            tcp_abort(pcb_);
    }
    pcb_   = nullptr;
    state_ = State::closed;
}

void Esp8266TcpSocket::on_connected()
{
    state_ = State::connected;
}

void Esp8266TcpSocket::on_error(std::int8_t error)
{
    // lwIP has already freed the connection.
    pcb_     = nullptr;
    state_   = State::failed;
    failure_ = error == ERR_TIMEOUT ? Status::timeout : Status::error;
}

void Esp8266TcpSocket::on_received(pbuf* data)
{
    if (data == nullptr)
    {
        state_ = State::peer_closed;
        return;
    }
    if (received_ == nullptr)
        received_ = data;
    else
        pbuf_cat(received_, data);
}

Status Esp8266TcpSocket::fail_(Status status)
{
    close();
    state_   = State::failed;
    failure_ = status;
    return status;
}

void Esp8266TcpSocket::release_received_()
{
    if (received_ != nullptr)
        pbuf_free(received_);
    received_ = nullptr;
}

}  // namespace brick::platform::esp8266
