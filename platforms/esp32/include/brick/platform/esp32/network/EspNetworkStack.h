#pragma once

#include "brick/Status.h"
#include "brick/interfaces/network/NetworkTypes.h"
#include "esp_netif.h"

// Helpers shared by the ESP-IDF network drivers (Wi-Fi, Ethernet, sockets).
namespace brick::platform::esp32
{

/// Starts the TCP/IP stack and the default event loop. Every network driver
/// calls it; repeated calls do nothing.
Status start_network_stack();

interfaces::network::Ipv4Address to_ipv4(const esp_ip4_addr_t& address);

/// Addresses of a netif with the main DNS server; Status::not_ready while it
/// has no address.
Status read_ip_info(esp_netif_t* netif, interfaces::network::IpInfo& info);

/// Routes traffic for other networks through netif.
Status make_default_route(esp_netif_t* netif);

}  // namespace brick::platform::esp32
