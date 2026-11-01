# BRICK ESP32 Platform

ESP32 adapters for BRICK interfaces. These headers intentionally depend on
ESP-IDF and are kept outside `brick-core` and `brick-interfaces`.

The P4 adapters include standalone MIPI-DSI and GT911 drivers plus JC1060
1024x600 hardware profiles. They are independent of ESPHome components.

`network/` holds Wi-Fi (`EspWifi`: station and hotspot; on the ESP32-P4
through the ESP32-C6 co-processor with `esp_wifi_remote` + `esp_hosted`),
Ethernet on the internal EMAC (`EspEthernet`), a non-blocking DNS resolver and
TCP socket. See [docs/network.md](../../docs/network.md).
