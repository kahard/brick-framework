#pragma once

#include <cstddef>

#include "brick/interfaces/network/WifiTypes.h"

namespace brick::core::network
{

inline constexpr std::size_t kNoWifiNetwork = static_cast<std::size_t>(-1);

/// Index of the known network to join after a scan (visible: strongest
/// first), or kNoWifiNetwork. Picks the strongest known network in range,
/// avoiding last_failed while another one is in range; with none in range,
/// the next hidden network after last_failed.
std::size_t choose_wifi_network(const interfaces::network::WifiCredentials* known, std::size_t known_count,
                                const interfaces::network::WifiNetwork* visible, std::size_t visible_count,
                                std::size_t last_failed);

}  // namespace brick::core::network
