#include "brick/core/network/WifiScanList.h"

#include "brick/core/network/WifiConfigCheck.h"

namespace brick::core::network
{

namespace
{
    /// Index of the entry with the same SSID, count when there is none.
    /// Hidden networks (empty SSID) never match each other.
    std::size_t find_ssid(const interfaces::network::WifiNetwork* networks, std::size_t count, const char* ssid)
    {
        if (ssid[0] == '\0')
            return count;
        for (std::size_t index = 0; index < count; ++index)
            if (same_ssid(networks[index].ssid, ssid))
                return index;
        return count;
    }

    void remove_at(interfaces::network::WifiNetwork* networks, std::size_t& count, std::size_t index)
    {
        for (std::size_t next = index + 1; next < count; ++next)
            networks[next - 1] = networks[next];
        --count;
    }
}  // namespace

void add_scanned_network(interfaces::network::WifiNetwork* networks, std::size_t capacity, std::size_t& count,
                         const interfaces::network::WifiNetwork& network)
{
    if (networks == nullptr || capacity == 0)
        return;
    if (const std::size_t same = find_ssid(networks, count, network.ssid); same < count)
    {
        if (networks[same].rssi_dbm >= network.rssi_dbm)
            return;
        remove_at(networks, count, same);
    }
    if (count == capacity)
    {
        if (networks[count - 1].rssi_dbm >= network.rssi_dbm)
            return;
        --count;
    }
    std::size_t position = count;
    while (position > 0 && networks[position - 1].rssi_dbm < network.rssi_dbm)
    {
        networks[position] = networks[position - 1];
        --position;
    }
    networks[position] = network;
    ++count;
}

}  // namespace brick::core::network
