#include "brick/platform/esp8266/network/Esp8266DnsResolver.h"

#include <lwip/dns.h>
#include <lwip/ip_addr.h>

#include "brick/core/network/WifiConfigCheck.h"

namespace brick::platform::esp8266
{

namespace
{
    brick::interfaces::network::Ipv4Address to_address(const ip_addr_t& address)
    {
        const ip4_addr_t* ip4 = ip_2_ip4(&address);
        return { { static_cast<std::uint8_t>(ip4_addr1(ip4)), static_cast<std::uint8_t>(ip4_addr2(ip4)),
                   static_cast<std::uint8_t>(ip4_addr3(ip4)), static_cast<std::uint8_t>(ip4_addr4(ip4)) } };
    }

    void on_found(const char*, const ip_addr_t* address, void* context)
    {
        Esp8266DnsQuery& query = *static_cast<Esp8266DnsQuery*>(context);
        const bool       found = address != nullptr && IP_IS_V4(address);
        query.pending          = false;
        query.owner->complete(query.id, found,
                              found ? to_address(*address) : brick::interfaces::network::Ipv4Address{});
    }
}  // namespace

Esp8266DnsResolver::Esp8266DnsResolver()
{
    for (Esp8266DnsQuery& query : queries_)
        query.owner = this;
}

Status Esp8266DnsResolver::resolve(const char* host_name)
{
    const std::size_t length = brick::core::network::bounded_length(host_name, kMaxHostNameLength);
    if (length == 0 || length > kMaxHostNameLength)
        return Status::invalid_argument;
    Esp8266DnsQuery* query = free_query_();
    if (query == nullptr)
        return Status::busy;
    if (const Status status = brick::core::network::copy_text(name_, sizeof(name_), host_name); !is_ok(status))
        return status;
    next_id_       = next_id_ + 1 == 0 ? 1 : next_id_ + 1;
    query->id      = next_id_;
    query->pending = true;
    current_id_    = next_id_;
    state_         = State::pending;
    ip_addr_t   address{};
    const err_t error = dns_gethostbyname(name_, &address, on_found, query);
    if (error == ERR_INPROGRESS)
        return Status::ok;
    query->pending = false;
    if (error == ERR_OK)
    {
        complete(query->id, true, to_address(address));  // a cached name or a dotted address
        return Status::ok;
    }
    state_ = State::idle;
    // ERR_MEM: every lwIP lookup slot is in use.
    return error == ERR_MEM ? Status::busy : Status::error;
}

Status Esp8266DnsResolver::result(brick::interfaces::network::Ipv4Address& address)
{
    switch (state_)
    {
        case State::idle:
            return Status::not_ready;
        case State::pending:
            return Status::busy;
        case State::not_found:
            return Status::not_found;
        case State::found:
            break;
    }
    address = address_;
    return Status::ok;
}

void Esp8266DnsResolver::cancel()
{
    current_id_ = 0;
    state_      = State::idle;
}

void Esp8266DnsResolver::complete(std::uint32_t id, bool found, const brick::interfaces::network::Ipv4Address& address)
{
    if (id != current_id_)
        return;
    address_ = address;
    state_   = found ? State::found : State::not_found;
}

Esp8266DnsQuery* Esp8266DnsResolver::free_query_()
{
    for (Esp8266DnsQuery& query : queries_)
        if (!query.pending)
            return &query;
    return nullptr;
}

}  // namespace brick::platform::esp8266
