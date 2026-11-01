#include "brick/platform/esp32/network/EspNetworkStack.h"

#include "brick/platform/esp32/EspStatus.h"
#include "esp_event.h"

namespace brick::platform::esp32
{

Status start_network_stack()
{
    // esp_netif_init() does nothing when the stack already runs.
    if (const esp_err_t error = esp_netif_init(); error != ESP_OK)
        return to_status(error);
    // ESP_ERR_INVALID_STATE: another driver or the application created it.
    const esp_err_t error = esp_event_loop_create_default();
    return error == ESP_OK || error == ESP_ERR_INVALID_STATE ? Status::ok : to_status(error);
}

interfaces::network::Ipv4Address to_ipv4(const esp_ip4_addr_t& address)
{
    return { { static_cast<std::uint8_t>(esp_ip4_addr1(&address)), static_cast<std::uint8_t>(esp_ip4_addr2(&address)),
               static_cast<std::uint8_t>(esp_ip4_addr3(&address)),
               static_cast<std::uint8_t>(esp_ip4_addr4(&address)) } };
}

Status read_ip_info(esp_netif_t* netif, interfaces::network::IpInfo& info)
{
    if (netif == nullptr)
        return Status::not_ready;
    esp_netif_ip_info_t ip{};
    if (const esp_err_t error = esp_netif_get_ip_info(netif, &ip); error != ESP_OK)
        return to_status(error);
    if (ip.ip.addr == 0)
        return Status::not_ready;
    info.address = to_ipv4(ip.ip);
    info.netmask = to_ipv4(ip.netmask);
    info.gateway = to_ipv4(ip.gw);
    info.dns     = {};
    esp_netif_dns_info_t dns{};
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK && dns.ip.type == ESP_IPADDR_TYPE_V4)
        info.dns = to_ipv4(dns.ip.u_addr.ip4);
    return Status::ok;
}

Status make_default_route(esp_netif_t* netif)
{
    return netif == nullptr ? Status::not_ready : to_status(esp_netif_set_default_netif(netif));
}

}  // namespace brick::platform::esp32
