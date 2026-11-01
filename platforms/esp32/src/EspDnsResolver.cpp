#include "brick/platform/esp32/network/EspDnsResolver.h"

#include "brick/core/network/WifiConfigCheck.h"
#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/network/EspNetworkStack.h"
#include "esp_netif.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

namespace brick::platform::esp32
{

namespace
{
    /// Arguments and result of dns_gethostbyname(), run in the lwIP thread.
    struct LookupRequest
    {
        const char*  name;
        EspDnsQuery* query;
        ip_addr_t    address;
        err_t        result;
    };

    interfaces::network::Ipv4Address to_address(const ip_addr_t& address)
    {
        const ip4_addr_t* ip4 = ip_2_ip4(&address);
        return { { static_cast<std::uint8_t>(ip4_addr1(ip4)), static_cast<std::uint8_t>(ip4_addr2(ip4)),
                   static_cast<std::uint8_t>(ip4_addr3(ip4)), static_cast<std::uint8_t>(ip4_addr4(ip4)) } };
    }

    void on_found(const char*, const ip_addr_t* address, void* context)
    {
        EspDnsQuery& query = *static_cast<EspDnsQuery*>(context);
        const bool   found = address != nullptr && IP_IS_V4(address);
        query.owner->complete(query.id, found, found ? to_address(*address) : interfaces::network::Ipv4Address{});
        query.pending.store(false);
    }

    esp_err_t start_lookup(void* context)
    {
        LookupRequest& request = *static_cast<LookupRequest*>(context);
        request.result =
            dns_gethostbyname_addrtype(request.name, &request.address, on_found, request.query, LWIP_DNS_ADDRTYPE_IPV4);
        return ESP_OK;
    }

    std::uint32_t pack(const interfaces::network::Ipv4Address& address)
    {
        return static_cast<std::uint32_t>(address.octets[0]) << 24 | static_cast<std::uint32_t>(address.octets[1]) << 16
               | static_cast<std::uint32_t>(address.octets[2]) << 8 | address.octets[3];
    }
}  // namespace

EspDnsResolver::EspDnsResolver()
{
    for (EspDnsQuery& query : queries_)
        query.owner = this;
}

Status EspDnsResolver::resolve(const char* host_name)
{
    const std::size_t length = core::network::bounded_length(host_name, kMaxHostNameLength);
    if (length == 0 || length > kMaxHostNameLength)
        return Status::invalid_argument;
    if (const Status status = start_network_stack(); !is_ok(status))
        return status;
    EspDnsQuery* query = free_query_();
    if (query == nullptr)
        return Status::busy;
    if (const Status status = core::network::copy_text(name_, sizeof(name_), host_name); !is_ok(status))
        return status;
    next_id_  = next_id_ + 1 == 0 ? 1 : next_id_ + 1;
    query->id = next_id_;
    current_id_.store(next_id_);
    state_.store(State::pending);
    query->pending.store(true);
    LookupRequest   request{ name_, query, {}, ERR_OK };
    const esp_err_t error = esp_netif_tcpip_exec(start_lookup, &request);
    if (error == ESP_OK && request.result == ERR_INPROGRESS)
        return Status::ok;
    query->pending.store(false);
    if (error == ESP_OK && request.result == ERR_OK)
    {
        complete(query->id, true, to_address(request.address));  // a cached name or a dotted address
        return Status::ok;
    }
    state_.store(State::idle);
    // ERR_MEM: every lwIP lookup slot is in use.
    return error != ESP_OK ? to_status(error) : request.result == ERR_MEM ? Status::busy : Status::error;
}

Status EspDnsResolver::result(interfaces::network::Ipv4Address& address)
{
    switch (state_.load())
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
    const std::uint32_t packed = address_.load();
    address                    = { { static_cast<std::uint8_t>(packed >> 24), static_cast<std::uint8_t>(packed >> 16),
                                     static_cast<std::uint8_t>(packed >> 8), static_cast<std::uint8_t>(packed) } };
    return Status::ok;
}

void EspDnsResolver::cancel()
{
    current_id_.store(0);
    state_.store(State::idle);
}

void EspDnsResolver::complete(std::uint32_t id, bool found, const interfaces::network::Ipv4Address& address)
{
    if (id != current_id_.load())
        return;
    // The address first: result() reads the state, then the address.
    address_.store(pack(address));
    state_.store(found ? State::found : State::not_found);
}

EspDnsQuery* EspDnsResolver::free_query_()
{
    for (EspDnsQuery& query : queries_)
        if (!query.pending.load())
            return &query;
    return nullptr;
}

}  // namespace brick::platform::esp32
