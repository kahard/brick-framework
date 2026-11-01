#pragma once

#include <atomic>
#include <cstdint>

#include "brick/interfaces/network/INetworkInterface.h"
#include "esp_eth_driver.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_netif.h"

namespace brick::platform::esp32
{

/// PHY chips the ESP-IDF Ethernet driver supports on the internal EMAC.
enum class EthernetPhy : std::uint8_t
{
    generic,  ///< any IEEE 802.3 PHY, without vendor extras
    ip101,
    rtl8201,
    lan87xx,
    dp83848,
    ksz80xx,
};

/// Internal EMAC with an RMII PHY. GPIO numbers; -1 = not connected.
struct EspEthernetConfig
{
    EthernetPhy  phy             = EthernetPhy::generic;
    std::int32_t phy_address     = -1;  ///< -1: find the PHY on the management bus
    std::int32_t phy_reset_gpio  = -1;
    std::int32_t mdc_gpio        = -1;
    std::int32_t mdio_gpio       = -1;
    std::int32_t rmii_clock_gpio = -1;  ///< 50 MHz reference clock input
    // RMII data pins; used on the ESP32-P4, fixed on the ESP32.
    std::int32_t tx_en_gpio  = -1;
    std::int32_t txd0_gpio   = -1;
    std::int32_t txd1_gpio   = -1;
    std::int32_t crs_dv_gpio = -1;
    std::int32_t rxd0_gpio   = -1;
    std::int32_t rxd1_gpio   = -1;
    /// Name sent to the DHCP server; nullptr keeps the ESP-IDF default.
    const char* hostname = nullptr;
};

/// Ethernet on the EMAC of the ESP32 / ESP32-P4. begin() starts the driver;
/// the link comes up by itself when a cable is plugged in (link_state()
/// connecting, then up with a DHCP address) and goes down when it is pulled.
class EspEthernet final : public interfaces::network::INetworkInterface
{
public:
    explicit EspEthernet(const EspEthernetConfig& config) : config_(config) {}
    ~EspEthernet();
    EspEthernet(const EspEthernet&)            = delete;
    EspEthernet& operator=(const EspEthernet&) = delete;

    /// Status::not_supported on a chip without an EMAC.
    Status begin();

    const char*                    name() const override { return "ethernet"; }
    interfaces::network::LinkState link_state() const override { return state_.load(); }
    Status                         ip_info(interfaces::network::IpInfo& info) const override;
    Status                         mac_address(interfaces::network::MacAddress& mac) const override;
    Status                         set_default_route() override;

private:
    static void on_event_(void* context, esp_event_base_t base, std::int32_t id, void* data);
    Status      install_driver_();
    Status      register_events_();
    void        on_ethernet_event_(std::int32_t id, const void* data);

    EspEthernetConfig                           config_;
    esp_netif_t*                                netif_      = nullptr;
    esp_eth_handle_t                            driver_     = nullptr;
    esp_eth_netif_glue_handle_t                 glue_       = nullptr;
    esp_event_handler_instance_t                eth_events_ = nullptr;
    esp_event_handler_instance_t                ip_events_  = nullptr;
    std::atomic<interfaces::network::LinkState> state_{ interfaces::network::LinkState::down };
    bool                                        started_ = false;
};

}  // namespace brick::platform::esp32
