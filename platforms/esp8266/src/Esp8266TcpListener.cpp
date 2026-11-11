#include "brick/platform/esp8266/network/Esp8266TcpListener.h"

#include <lwip/err.h>
#include <lwip/ip_addr.h>
#include <lwip/tcp.h>

namespace brick::platform::esp8266
{

namespace
{
    err_t on_accept(void* context, tcp_pcb* pcb, err_t error)
    {
        if (error != ERR_OK || pcb == nullptr || context == nullptr)
            return ERR_VAL;
        if (static_cast<Esp8266TcpListener*>(context)->on_accepted(pcb))
            return ERR_OK;
        tcp_abort(pcb);
        return ERR_ABRT;
    }
}  // namespace

Status Esp8266TcpListener::listen(std::uint16_t port)
{
    stop_listening_();
    tcp_pcb* const pcb = tcp_new();
    if (pcb == nullptr)
        return Status::no_memory;
    if (const err_t error = tcp_bind(pcb, IP_ADDR_ANY, port); error != ERR_OK)
    {
        tcp_close(pcb);
        return error == ERR_USE ? Status::busy : Status::error;
    }
    // tcp_listen() frees pcb and returns the smaller listening one.
    listening_ = tcp_listen(pcb);
    if (listening_ == nullptr)
    {
        tcp_close(pcb);
        return Status::no_memory;
    }
    tcp_arg(listening_, this);
    tcp_accept(listening_, on_accept);
    return Status::ok;
}

brick::interfaces::network::ITcpSocket* Esp8266TcpListener::accept()
{
    for (std::size_t index = 0; index < kMaxConnections; ++index)
        if (waiting_[index])
        {
            waiting_[index] = false;
            return &sockets_[index];
        }
    return nullptr;
}

void Esp8266TcpListener::close()
{
    stop_listening_();
    for (std::size_t index = 0; index < kMaxConnections; ++index)
    {
        sockets_[index].close();
        waiting_[index] = false;
    }
}

bool Esp8266TcpListener::on_accepted(tcp_pcb* pcb)
{
    for (std::size_t index = 0; index < kMaxConnections; ++index)
        if (!waiting_[index] && sockets_[index].idle())
        {
            sockets_[index].adopt(pcb);
            waiting_[index] = true;
            return true;
        }
    return false;
}

void Esp8266TcpListener::stop_listening_()
{
    if (listening_ == nullptr)
        return;
    tcp_arg(listening_, nullptr);
    tcp_accept(listening_, nullptr);
    tcp_close(listening_);
    listening_ = nullptr;
}

}  // namespace brick::platform::esp8266
