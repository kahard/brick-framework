#pragma once

#include <array>
#include <cstdint>

namespace brick::interfaces::network
{

/// IPv4 address, most significant octet first: {192, 168, 4, 1}.
struct Ipv4Address
{
    std::array<std::uint8_t, 4> octets{};

    /// 0.0.0.0: no address assigned.
    constexpr bool unspecified() const { return octets[0] == 0 && octets[1] == 0 && octets[2] == 0 && octets[3] == 0; }
};

constexpr bool operator==(const Ipv4Address& left, const Ipv4Address& right)
{
    return left.octets[0] == right.octets[0] && left.octets[1] == right.octets[1] && left.octets[2] == right.octets[2]
           && left.octets[3] == right.octets[3];
}

constexpr bool operator!=(const Ipv4Address& left, const Ipv4Address& right)
{
    return !(left == right);
}

/// Address of a TCP or UDP service.
struct Endpoint
{
    Ipv4Address   address;
    std::uint16_t port = 0;
};

/// Addresses of an interface that is up.
struct IpInfo
{
    Ipv4Address address;
    Ipv4Address netmask;
    Ipv4Address gateway;
    Ipv4Address dns;  ///< first DNS server; unspecified when none was assigned
};

/// Hardware (MAC) address, first transmitted octet first.
struct MacAddress
{
    std::array<std::uint8_t, 6> octets{};
};

/// Connection state of a network interface.
enum class LinkState : std::uint8_t
{
    down,        ///< no cable, no access point joined, or stopped
    connecting,  ///< the link is being established or has no IP address yet
    up,          ///< has an IP address: traffic can flow
};

/// Constant name of the state for logs and screens, e.g. "up".
constexpr const char* to_string(LinkState state)
{
    switch (state)
    {
        case LinkState::down:
            return "down";
        case LinkState::connecting:
            return "connecting";
        case LinkState::up:
            return "up";
    }
    return "unknown";
}

}  // namespace brick::interfaces::network
