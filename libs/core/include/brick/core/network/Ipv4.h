#pragma once

#include <cstddef>

#include "brick/Status.h"
#include "brick/interfaces/network/NetworkTypes.h"

namespace brick::core::network
{

/// Room for "255.255.255.255" and its NUL.
inline constexpr std::size_t kIpv4TextCapacity = 16;
/// Room for "AA:BB:CC:DD:EE:FF" and its NUL.
inline constexpr std::size_t kMacTextCapacity = 18;

/// Writes the dotted form, e.g. "192.168.4.1". Status::no_memory when text
/// is shorter than kIpv4TextCapacity.
Status format_ipv4(const interfaces::network::Ipv4Address& address, char* text, std::size_t capacity);
/// Reads a dotted address; false for anything else ("1.2.3", "256.1.1.1").
bool parse_ipv4(const char* text, interfaces::network::Ipv4Address& address);
/// Writes "AA:BB:CC:DD:EE:FF". Status::no_memory when text is shorter than
/// kMacTextCapacity.
Status format_mac(const interfaces::network::MacAddress& mac, char* text, std::size_t capacity);

}  // namespace brick::core::network
