#include "brick/core/network/Ipv4.h"

#include <cstdio>

namespace brick::core::network
{

namespace
{
    constexpr unsigned kMaxOctet = 255;

    bool is_digit(char character)
    {
        return character >= '0' && character <= '9';
    }

    /// Reads one decimal octet (1-3 digits, at most 255) and moves text past it.
    bool parse_octet(const char*& text, std::uint8_t& octet)
    {
        unsigned    value  = 0;
        std::size_t digits = 0;
        while (is_digit(*text) && digits < 3)
        {
            value = value * 10 + static_cast<unsigned>(*text - '0');
            ++text;
            ++digits;
        }
        if (digits == 0 || value > kMaxOctet || is_digit(*text))
            return false;
        octet = static_cast<std::uint8_t>(value);
        return true;
    }
}  // namespace

Status format_ipv4(const interfaces::network::Ipv4Address& address, char* text, std::size_t capacity)
{
    if (text == nullptr || capacity < kIpv4TextCapacity)
        return Status::no_memory;
    std::snprintf(text, capacity, "%u.%u.%u.%u", static_cast<unsigned>(address.octets[0]),
                  static_cast<unsigned>(address.octets[1]), static_cast<unsigned>(address.octets[2]),
                  static_cast<unsigned>(address.octets[3]));
    return Status::ok;
}

bool parse_ipv4(const char* text, interfaces::network::Ipv4Address& address)
{
    if (text == nullptr)
        return false;
    interfaces::network::Ipv4Address parsed;
    for (std::size_t index = 0; index < parsed.octets.size(); ++index)
    {
        if (index > 0 && *text++ != '.')
            return false;
        if (!parse_octet(text, parsed.octets[index]))
            return false;
    }
    if (*text != '\0')
        return false;
    address = parsed;
    return true;
}

Status format_mac(const interfaces::network::MacAddress& mac, char* text, std::size_t capacity)
{
    if (text == nullptr || capacity < kMacTextCapacity)
        return Status::no_memory;
    std::snprintf(text, capacity, "%02X:%02X:%02X:%02X:%02X:%02X", static_cast<unsigned>(mac.octets[0]),
                  static_cast<unsigned>(mac.octets[1]), static_cast<unsigned>(mac.octets[2]),
                  static_cast<unsigned>(mac.octets[3]), static_cast<unsigned>(mac.octets[4]),
                  static_cast<unsigned>(mac.octets[5]));
    return Status::ok;
}

}  // namespace brick::core::network
