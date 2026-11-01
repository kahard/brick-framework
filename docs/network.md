# Networking

BRICK reaches the network in three layers, the same way it reaches a
display: interfaces describe what an application may use, portable classes in
`libs/core` manage it, and each platform implements the interfaces on its SDK.
An application never sees ESP-IDF, lwIP or Arduino types, so the same code runs
on an ESP32-S3 over Wi-Fi, on an ESP32-P4 over Ethernet and in the PC
simulator.

```text
application        WifiManager   NetworkSelector   HttpProbe        (libs/core, portable)
                        |               |               |
interfaces         IWifiStation  INetworkInterface  IDnsResolver, ITcpSocket
                   IWifiAccessPoint
                        |               |               |
platforms          EspWifi       EspEthernet       EspDnsResolver, EspTcpSocket     (ESP32 family)
                   Esp8266Wifi                     Esp8266DnsResolver, Esp8266TcpSocket
                   PcWifi        PcHostNetwork     PcDnsResolver, PcTcpSocket       (PC simulator)
```

Everything is IPv4 and non-blocking: an operation is started, then polled from
the loop (`update()` / `poll_connect()` / `result()`), like every other BRICK
driver (rule H6).

## Interfaces (`brick/interfaces/network`)

| Interface | What it is |
|---|---|
| `INetworkInterface` | Any link traffic can go through: name, `link_state()` (`down`, `connecting`, `up` = has an address), `ip_info()`, `mac_address()`, `set_default_route()`. Code that only needs "the internet" depends on this. |
| `IWifiStation` | An `INetworkInterface` that scans (`start_scan()`, `scan_results()`) and joins a network (`connect()`, `disconnect()`, `failure()`, `connected_network()`). It does not retry by itself. |
| `IWifiAccessPoint` | An `INetworkInterface` that runs a hotspot (`start()`, `stop()`, `client_count()`), alongside the station of the same radio. |
| `IDnsResolver` | One host-name lookup at a time: `resolve()`, then `result()` until it is no longer `busy`. |
| `ITcpSocket` | A TCP client: `connect()`, `poll_connect()`, `send()`, `receive()` (`no_data` while nothing arrived, `not_ready` once the server closed the connection), `close()`. |

`NetworkTypes.h` and `WifiTypes.h` hold the plain data: `Ipv4Address`,
`IpInfo`, `MacAddress`, `Endpoint`, `WifiNetwork` (a scan result),
`WifiCredentials`, `WifiAccessPointConfig`, `WifiFailure`.

## Portable management (`brick/core/network`)

`WifiManager` keeps a station connected:

- scans, joins the strongest known network in range (a network that just
  failed is tried again only when no other is in range; hidden networks are
  joined without being seen),
- retries with a growing pause (`retry_delay_ms`, doubled up to
  `max_retry_delay_ms`) and rejoins after a lost connection,
- opens a fallback hotspot after `attempts_before_fallback` failures in a row,
  or at once when no network is configured, and closes it when the station is
  connected again,
- keeps the networks of the last scan (`networks()`, `request_scan()`) for a
  list on the screen.

`NetworkSelector` is the common interface for using the internet: given links
in priority order (for example Ethernet, then Wi-Fi) it picks the first one
that is up, makes it the default route whenever the choice changes and is an
`INetworkInterface` itself. An application receives one network and does not
care which link carries its traffic.

`HttpProbe` tells "joined a network" from "the internet works": it resolves a
host, connects, sends `GET` and reads the status code, with a time limit, and
reports how long each step took. Against
`connectivitycheck.gstatic.com/generate_204` a working connection answers
204; a captive portal answers something else.

`Ipv4.h` formats and parses addresses; `WifiConfigCheck.h` and
`WifiScanList.h` are the checks and the scan list every driver shares, so all
platforms accept the same credentials and sort scans the same way.

```cpp
// Target entry point: the board owns the links, the target the clients.
static brick::platform::esp32::EspDnsResolver dns;
static brick::platform::esp32::EspTcpSocket   socket;

// Application: only interfaces and core.
const WifiCredentials known[] = { { "Home", "secret-password" } };
const WifiAccessPointConfig setup{ "DEVICE-SETUP", "device-setup" };
WifiManagerConfig config;
config.networks              = known;
config.network_count         = 1;
config.fallback_access_point = &setup;
wifi_manager.start(config);              // then wifi_manager.update() in the loop

INetworkInterface* links[] = { &ethernet, &wifi_station };
NetworkSelector    network(links, 2);    // network.update() in the loop
probe.start("connectivitycheck.gstatic.com", "/generate_204");
```

## Platforms

### ESP32 family (ESP-IDF)

`EspWifi` owns the Wi-Fi driver, its two netifs and event handlers, and offers
`station()` (`EspWifiStation`) and `access_point()` (`EspWifiAccessPoint`).
`begin()` starts the radio as a station; credentials stay in RAM
(`WIFI_STORAGE_RAM`), the application owns them. Events arrive in the ESP-IDF
event task, so the state they change is atomic.

On the ESP32-P4 (JC1060, JC8012) the radio is an ESP32-C6 on SDIO. The same
classes work through the `esp_wifi_remote` and `esp_hosted` components, which
the application adds in `src/idf_component.yml`, with the SDIO settings in
`sdkconfig.defaults` (see the network test applications). `esp_hosted` starts
itself.

`EspEthernet` drives the internal EMAC with an RMII PHY (IP101, RTL8201,
LAN87xx, DP83848, KSZ80xx or generic); the board profile supplies the pins.
`begin()` returns at once; the link follows the cable.

`EspDnsResolver` starts lwIP lookups in the TCP/IP thread
(`esp_netif_tcpip_exec`) and gets the answer there; `EspTcpSocket` is a
non-blocking lwIP socket.

### ESP8266 (Arduino)

`Esp8266Wifi` with `Esp8266WifiStation` and `Esp8266WifiAccessPoint` over the
ESP8266WiFi library (asynchronous scan, `WiFi.begin()` without waiting, no
`String`). `Esp8266DnsResolver` and `Esp8266TcpSocket` use the raw lwIP API,
which the ESP8266 runs in the same cooperative context as the loop. The
application lists `ESP8266WiFi` in `lib_deps`.

### PC simulator

`SimulatorBoard::wifi()` is a simulated radio (`PcWifi`): in simulated time a
scan finds the networks given with `--wifi SSID[:PASS[:RSSI]]` (default
"BRICK-LAB" / "brick-lab-pass", a neighbour and an open network) and joining
succeeds only with the right password, so headless runs are deterministic.
`SimulatorBoard::ethernet()` is the PC's own connection (`PcHostNetwork`).
`PcDnsResolver` and `PcTcpSocket` use the real network; with `--offline` they
fail at once, which keeps CI independent of the internet.

### Boards

| Board | Flags in `Features` | Accessors |
|---|---|---|
| CYD, Panel 480, JC8012 | `wifi` | `wifi()` |
| JC1060 | `wifi`, `ethernet` | `wifi()`, `ethernet()` |
| ESP-12F ST7789 | `wifi` | `wifi()` |
| PC simulator | always | `wifi()`, `ethernet()` |

The flags default to off, so existing `Features` structs keep compiling and
keep their RAM; `begin()` starts what is enabled. STM32F1 boards have no
network hardware.

## Limits

- IPv4 only, plain TCP; no TLS (HTTPS) yet.
- Credentials are not stored by BRICK; an application keeps them (for example
  from its build or its own storage).
- The fallback hotspot has no web page for entering credentials yet.
