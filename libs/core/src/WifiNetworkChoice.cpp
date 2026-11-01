#include "brick/core/network/WifiNetworkChoice.h"

#include "brick/core/network/WifiConfigCheck.h"

namespace brick::core::network
{

namespace
{
    struct Scan
    {
        const interfaces::network::WifiNetwork* networks;
        std::size_t                             count;

        /// Scan results come strongest first, so the first match is the best one.
        bool signal_of(const char* ssid, std::int8_t& rssi_dbm) const
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                if (same_ssid(networks[index].ssid, ssid))
                {
                    rssi_dbm = networks[index].rssi_dbm;
                    return true;
                }
            }
            return false;
        }
    };

    std::size_t best_visible(const interfaces::network::WifiCredentials* known, std::size_t known_count,
                             const Scan& scan, std::size_t skip)
    {
        std::size_t best      = kNoWifiNetwork;
        std::int8_t best_rssi = 0;
        for (std::size_t index = 0; index < known_count; ++index)
        {
            std::int8_t rssi_dbm = 0;
            if (index == skip || !scan.signal_of(known[index].ssid, rssi_dbm))
                continue;
            if (best == kNoWifiNetwork || rssi_dbm > best_rssi)
            {
                best      = index;
                best_rssi = rssi_dbm;
            }
        }
        return best;
    }

    std::size_t next_hidden(const interfaces::network::WifiCredentials* known, std::size_t known_count,
                            std::size_t last_failed)
    {
        const std::size_t first = last_failed == kNoWifiNetwork ? 0 : last_failed + 1;
        for (std::size_t step = 0; step < known_count; ++step)
        {
            const std::size_t index = (first + step) % known_count;
            if (known[index].hidden)
                return index;
        }
        return kNoWifiNetwork;
    }
}  // namespace

std::size_t choose_wifi_network(const interfaces::network::WifiCredentials* known, std::size_t known_count,
                                const interfaces::network::WifiNetwork* visible, std::size_t visible_count,
                                std::size_t last_failed)
{
    if (known == nullptr || known_count == 0)
        return kNoWifiNetwork;
    const Scan  scan{ visible, visible == nullptr ? 0 : visible_count };
    std::size_t index = best_visible(known, known_count, scan, last_failed);
    if (index == kNoWifiNetwork)
        index = best_visible(known, known_count, scan, kNoWifiNetwork);
    if (index == kNoWifiNetwork)
        index = next_hidden(known, known_count, last_failed);
    return index;
}

}  // namespace brick::core::network
