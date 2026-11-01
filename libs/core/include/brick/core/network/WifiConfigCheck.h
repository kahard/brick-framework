#pragma once

#include <cstddef>

#include "brick/Status.h"
#include "brick/interfaces/network/WifiTypes.h"

// Checks shared by every Wi-Fi driver and the manager, so all platforms
// accept and reject the same credentials.
namespace brick::core::network
{

/// Length of text, counting at most limit + 1 characters (enough to tell
/// "too long"); 0 for nullptr.
constexpr std::size_t bounded_length(const char* text, std::size_t limit)
{
    std::size_t length = 0;
    while (text != nullptr && length <= limit && text[length] != '\0')
        ++length;
    return length;
}

/// True for an empty password (open network) or a WPA passphrase.
constexpr bool valid_wifi_password(const char* password)
{
    const std::size_t length = bounded_length(password, interfaces::network::kWifiPasswordMaxLength);
    return length == 0
           || (length >= interfaces::network::kWifiPasswordMinLength
               && length <= interfaces::network::kWifiPasswordMaxLength);
}

constexpr bool valid_wifi_ssid(const char* ssid)
{
    const std::size_t length = bounded_length(ssid, interfaces::network::kWifiSsidMaxLength);
    return length > 0 && length <= interfaces::network::kWifiSsidMaxLength;
}

/// Status::invalid_argument for an empty or too long SSID or a password that
/// is neither empty nor 8..63 characters.
constexpr Status check_credentials(const interfaces::network::WifiCredentials& credentials)
{
    return valid_wifi_ssid(credentials.ssid) && valid_wifi_password(credentials.password) ? Status::ok :
                                                                                            Status::invalid_argument;
}

inline constexpr std::uint8_t kWifiMaxChannel = 13;

/// check_credentials() for a hotspot, plus a channel of 1..13 and room for
/// at least one client.
constexpr Status check_access_point(const interfaces::network::WifiAccessPointConfig& config)
{
    const bool valid = valid_wifi_ssid(config.ssid) && valid_wifi_password(config.password) && config.channel >= 1
                       && config.channel <= kWifiMaxChannel && config.max_clients > 0;
    return valid ? Status::ok : Status::invalid_argument;
}

/// True when both SSIDs are the same text (both may be nullptr).
constexpr bool same_ssid(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr)
        return left == right;
    std::size_t index = 0;
    while (left[index] != '\0' && left[index] == right[index])
        ++index;
    return left[index] == right[index];
}

/// Copies a NUL-terminated string into a fixed buffer.
/// Status::invalid_argument when it does not fit.
constexpr Status copy_text(char* destination, std::size_t capacity, const char* source)
{
    const std::size_t length = bounded_length(source, capacity);
    if (destination == nullptr || capacity == 0 || length >= capacity)
        return Status::invalid_argument;
    for (std::size_t index = 0; index < length; ++index)
        destination[index] = source[index];
    destination[length] = '\0';
    return Status::ok;
}

}  // namespace brick::core::network
