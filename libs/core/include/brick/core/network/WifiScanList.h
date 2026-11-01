#pragma once

#include <cstddef>

#include "brick/interfaces/network/WifiTypes.h"

namespace brick::core::network
{

/// Adds a scanned access point to a list kept strongest first, one entry per
/// SSID (the strongest access point of a network with several). When the
/// list is full the weakest entry makes room for a stronger one. Drivers use
/// it to build what IWifiStation::scan_results() returns.
void add_scanned_network(interfaces::network::WifiNetwork* networks, std::size_t capacity, std::size_t& count,
                         const interfaces::network::WifiNetwork& network);

}  // namespace brick::core::network
