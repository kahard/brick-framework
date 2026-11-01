#include "brick/platform/esp32/network/EspEthernet.h"

#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/network/EspNetworkStack.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace brick::platform::esp32
{

using interfaces::network::LinkState;

namespace
{
    constexpr char kTag[] = "brick_ethernet";

#if CONFIG_ETH_USE_ESP32_EMAC
    esp_eth_phy_t* new_phy(EthernetPhy phy, const eth_phy_config_t& config)
    {
        switch (phy)
        {
            case EthernetPhy::ip101:
                return esp_eth_phy_new_ip101(&config);
            case EthernetPhy::rtl8201:
                return esp_eth_phy_new_rtl8201(&config);
            case EthernetPhy::lan87xx:
                return esp_eth_phy_new_lan87xx(&config);
            case EthernetPhy::dp83848:
                return esp_eth_phy_new_dp83848(&config);
            case EthernetPhy::ksz80xx:
                return esp_eth_phy_new_ksz80xx(&config);
            case EthernetPhy::generic:
                break;
        }
        return esp_eth_phy_new_generic(&config);
    }

    eth_esp32_emac_config_t emac_config(const EspEthernetConfig& config)
    {
        eth_esp32_emac_config_t emac{};
        emac.smi_gpio.mdc_num             = config.mdc_gpio;
        emac.smi_gpio.mdio_num            = config.mdio_gpio;
        emac.interface                    = EMAC_DATA_INTERFACE_RMII;
        emac.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
        emac.clock_config.rmii.clock_gpio = static_cast<emac_rmii_clock_gpio_t>(config.rmii_clock_gpio);
        emac.dma_burst_len                = ETH_DMA_BURST_LEN_32;
#    if SOC_EMAC_USE_MULTI_IO_MUX || SOC_EMAC_MII_USE_GPIO_MATRIX
        emac.emac_dataif_gpio.rmii.tx_en_num  = config.tx_en_gpio;
        emac.emac_dataif_gpio.rmii.txd0_num   = config.txd0_gpio;
        emac.emac_dataif_gpio.rmii.txd1_num   = config.txd1_gpio;
        emac.emac_dataif_gpio.rmii.crs_dv_num = config.crs_dv_gpio;
        emac.emac_dataif_gpio.rmii.rxd0_num   = config.rxd0_gpio;
        emac.emac_dataif_gpio.rmii.rxd1_num   = config.rxd1_gpio;
#    endif
#    if !SOC_EMAC_RMII_CLK_OUT_INTERNAL_LOOPBACK
        emac.clock_config_out_in.rmii.clock_mode = EMAC_CLK_EXT_IN;
        emac.clock_config_out_in.rmii.clock_gpio = static_cast<emac_rmii_clock_gpio_t>(-1);
#    endif
        return emac;
    }
#endif
}  // namespace

EspEthernet::~EspEthernet()
{
    if (!started_)
        return;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_eth_stop(driver_));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_events_));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_events_));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_eth_del_netif_glue(glue_));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_eth_driver_uninstall(driver_));
    esp_netif_destroy(netif_);
}

Status EspEthernet::begin()
{
    if (started_)
        return Status::ok;
    if (const Status status = start_network_stack(); !is_ok(status))
        return status;
    const esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    netif_                                = esp_netif_new(&netif_config);
    if (netif_ == nullptr)
        return Status::no_memory;
    if (config_.hostname != nullptr)
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_hostname(netif_, config_.hostname));
    if (const Status status = install_driver_(); !is_ok(status))
        return status;
    glue_ = esp_eth_new_netif_glue(driver_);
    if (glue_ == nullptr)
        return Status::no_memory;
    if (const esp_err_t error = esp_netif_attach(netif_, glue_); error != ESP_OK)
        return to_status(error);
    if (const Status status = register_events_(); !is_ok(status))
        return status;
    if (const esp_err_t error = esp_eth_start(driver_); error != ESP_OK)
        return to_status(error);
    started_ = true;
    ESP_LOGI(kTag, "Ethernet started, waiting for a link");
    return Status::ok;
}

Status EspEthernet::install_driver_()
{
#if CONFIG_ETH_USE_ESP32_EMAC
    const eth_esp32_emac_config_t emac       = emac_config(config_);
    const eth_mac_config_t        mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t              phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr                      = config_.phy_address;
    phy_config.reset_gpio_num                = config_.phy_reset_gpio;
    esp_eth_mac_t*   mac                     = esp_eth_mac_new_esp32(&emac, &mac_config);
    esp_eth_phy_t*   phy                     = new_phy(config_.phy, phy_config);
    esp_eth_config_t eth_config              = ETH_DEFAULT_CONFIG(mac, phy);
    const esp_err_t  error =
        mac != nullptr && phy != nullptr ? esp_eth_driver_install(&eth_config, &driver_) : ESP_ERR_NO_MEM;
    if (error != ESP_OK)
    {
        if (mac != nullptr)
            mac->del(mac);
        if (phy != nullptr)
            phy->del(phy);
    }
    return to_status(error);
#else
    return Status::not_supported;
#endif
}

Status EspEthernet::register_events_()
{
    if (const esp_err_t error =
            esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_event_, this, &eth_events_);
        error != ESP_OK)
        return to_status(error);
    return to_status(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, on_event_, this, &ip_events_));
}

Status EspEthernet::ip_info(interfaces::network::IpInfo& info) const
{
    return state_.load() == LinkState::up ? read_ip_info(netif_, info) : Status::not_ready;
}

Status EspEthernet::mac_address(interfaces::network::MacAddress& mac) const
{
    return started_ ? to_status(esp_eth_ioctl(driver_, ETH_CMD_G_MAC_ADDR, mac.octets.data())) : Status::not_ready;
}

Status EspEthernet::set_default_route()
{
    return state_.load() == LinkState::up ? make_default_route(netif_) : Status::not_ready;
}

void EspEthernet::on_event_(void* context, esp_event_base_t base, std::int32_t id, void* data)
{
    EspEthernet& ethernet = *static_cast<EspEthernet*>(context);
    if (base == ETH_EVENT)
        ethernet.on_ethernet_event_(id, data);
    else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP && ethernet.state_.load() != LinkState::down)
        ethernet.state_.store(LinkState::up);
    else if (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP && ethernet.state_.load() == LinkState::up)
        ethernet.state_.store(LinkState::connecting);
}

void EspEthernet::on_ethernet_event_(std::int32_t id, const void* data)
{
    // Events of another Ethernet driver (e.g. an SPI module) are not ours.
    if (data == nullptr || *static_cast<const esp_eth_handle_t*>(data) != driver_)
        return;
    switch (id)
    {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(kTag, "Link up");
            if (state_.load() == LinkState::down)
                state_.store(LinkState::connecting);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(kTag, "Link down");
            state_.store(LinkState::down);
            break;
        default:
            break;
    }
}

}  // namespace brick::platform::esp32
