#pragma once

#include "brick/boards/esp32/p4/jc1060/InitSequence.h"
#include "brick/platform/esp32/network/EspEthernet.h"
#include "brick/platform/esp32/p4/MipiDsiDisplay.h"
#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#include "brick/platform/esp32/touch/Gt911Touchscreen.h"
#include "driver/gpio.h"

// Guition JC1060P470C-I-W: ESP32-P4, 7-inch 1024x600 MIPI-DSI panel (JD9165),
// GT911 touch, a 4-bit SDMMC card slot, an IP101 Ethernet PHY and an
// ESP32-C6 Wi-Fi co-processor on SDIO (esp_hosted defaults: CLK 18, CMD 19,
// D0-D3 14-17, reset 54).
namespace brick::boards::jc1060
{

inline constexpr gpio_num_t kBacklightGpio = GPIO_NUM_23;

inline brick::platform::esp32::p4::MipiDsiPanelConfig display_config()
{
    brick::platform::esp32::p4::MipiDsiPanelConfig config;
    config.width                   = 1024;
    config.height                  = 600;
    config.data_lanes              = 2;
    config.lane_bit_rate_mbps      = 600.0f;
    config.pixel_clock_mhz         = 40.0f;
    config.hsync_pulse_width       = 20;
    config.hsync_back_porch        = 160;
    config.hsync_front_porch       = 160;
    config.vsync_pulse_width       = 10;
    config.vsync_back_porch        = 23;
    config.vsync_front_porch       = 12;
    config.reset_gpio              = GPIO_NUM_27;
    config.mipi_dsi_ldo_channel    = 3;
    config.mipi_dsi_ldo_voltage_mv = 2500;
    config.init_sequence           = kInitSequence;
    config.init_sequence_size      = sizeof(kInitSequence);
    return config;
}

inline brick::platform::esp32::touch::Gt911Config touch_config()
{
    brick::platform::esp32::touch::Gt911Config config;
    config.port                             = I2C_NUM_0;
    config.sda_gpio                         = GPIO_NUM_7;
    config.scl_gpio                         = GPIO_NUM_8;
    config.display_size                     = { 1024, 600 };
    config.address                          = 0x5D;
    config.read_calibration_from_controller = true;
    return config;
}

/// IP101 on the internal EMAC (RMII, 50 MHz reference clock from the PHY).
inline brick::platform::esp32::EspEthernetConfig ethernet_config()
{
    brick::platform::esp32::EspEthernetConfig config;
    config.phy             = brick::platform::esp32::EthernetPhy::ip101;
    config.phy_address     = 1;
    config.phy_reset_gpio  = 51;
    config.mdc_gpio        = 31;
    config.mdio_gpio       = 52;
    config.rmii_clock_gpio = 50;
    config.tx_en_gpio      = 49;
    config.txd0_gpio       = 34;
    config.txd1_gpio       = 35;
    config.crs_dv_gpio     = 28;
    config.rxd0_gpio       = 29;
    config.rxd1_gpio       = 30;
    return config;
}

inline brick::platform::esp32::SdmmcFileSystemConfig sd_config()
{
    return {};
}

}  // namespace brick::boards::jc1060
