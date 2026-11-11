#include "brick/platform/esp8266/network/Esp8266UdpSocket.h"

#include <lwip/err.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>

namespace brick::platform::esp8266
{

namespace
{
    constexpr std::size_t kMaxDatagram = 0xFFFF;  ///< lwIP lengths are 16 bits

    void on_receive(void* context, udp_pcb*, pbuf* data, const ip_addr_t* address, u16_t port)
    {
        if (context == nullptr || address == nullptr)
        {
            pbuf_free(data);
            return;
        }
        brick::interfaces::network::Endpoint from;
        const ip4_addr_t*                    ip4 = ip_2_ip4(address);
        from.address.octets                      = { ip4_addr1(ip4), ip4_addr2(ip4), ip4_addr3(ip4), ip4_addr4(ip4) };
        from.port                                = port;
        static_cast<Esp8266UdpSocket*>(context)->on_received(data, from);
    }
}  // namespace

Status Esp8266UdpSocket::open(std::uint16_t port)
{
    close();
    pcb_ = udp_new();
    if (pcb_ == nullptr)
        return Status::no_memory;
    if (const err_t error = udp_bind(pcb_, IP_ADDR_ANY, port); error != ERR_OK)
    {
        udp_remove(pcb_);
        pcb_ = nullptr;
        return error == ERR_USE ? Status::busy : Status::error;
    }
    udp_recv(pcb_, on_receive, this);
    return Status::ok;
}

Status Esp8266UdpSocket::receive_from(std::uint8_t* data, std::size_t capacity, std::size_t& received,
                                      brick::interfaces::network::Endpoint& from)
{
    received = 0;
    if (pcb_ == nullptr)
        return Status::not_ready;
    if (capacity == 0)
        return Status::invalid_argument;
    if (count_ == 0)
        return Status::no_data;
    Datagram&         datagram = waiting_[first_];
    const std::size_t limit    = capacity < kMaxDatagram ? capacity : kMaxDatagram;
    received                   = pbuf_copy_partial(datagram.data, data, static_cast<u16_t>(limit), 0);
    from                       = datagram.from;
    pbuf_free(datagram.data);
    datagram.data = nullptr;
    first_        = (first_ + 1) % kMaxWaiting;
    --count_;
    return Status::ok;
}

Status Esp8266UdpSocket::send_to(const brick::interfaces::network::Endpoint& to, const std::uint8_t* data,
                                 std::size_t size)
{
    if (pcb_ == nullptr)
        return Status::not_ready;
    if (size > kMaxDatagram)
        return Status::invalid_argument;
    pbuf* const buffer = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(size), PBUF_RAM);
    if (buffer == nullptr)
        return Status::busy;
    pbuf_take(buffer, data, static_cast<u16_t>(size));
    ip_addr_t address{};
    IP_ADDR4(&address, to.address.octets[0], to.address.octets[1], to.address.octets[2], to.address.octets[3]);
    const err_t error = udp_sendto(pcb_, buffer, &address, to.port);
    pbuf_free(buffer);
    if (error == ERR_MEM)
        return Status::busy;
    return error == ERR_OK ? Status::ok : Status::error;
}

void Esp8266UdpSocket::close()
{
    if (pcb_ != nullptr)
    {
        udp_recv(pcb_, nullptr, nullptr);
        udp_remove(pcb_);
        pcb_ = nullptr;
    }
    while (count_ > 0)
    {
        pbuf_free(waiting_[first_].data);
        waiting_[first_].data = nullptr;
        first_                = (first_ + 1) % kMaxWaiting;
        --count_;
    }
    first_ = 0;
}

void Esp8266UdpSocket::on_received(pbuf* data, const brick::interfaces::network::Endpoint& from)
{
    if (count_ == kMaxWaiting)
    {
        pbuf_free(data);
        return;
    }
    Datagram& datagram = waiting_[(first_ + count_) % kMaxWaiting];
    datagram.data      = data;
    datagram.from      = from;
    ++count_;
}

}  // namespace brick::platform::esp8266
