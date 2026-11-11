# Networking

BRICK reaches the network in three layers, the same way it reaches a
display: interfaces describe what an application may use, portable classes in
`libs/core` manage it, and each platform implements the interfaces on its SDK.
An application never sees ESP-IDF, lwIP or Arduino types, so the same code runs
on an ESP32-S3 over Wi-Fi, on an ESP32-P4 over Ethernet and in the PC
simulator.

```text
application        WifiManager   NetworkSelector   HttpProbe   WifiSetupPortal (HttpServer, CaptiveDns,
                                                                WifiCredentialStore)   (libs/core, portable)
                        |               |               |               |
interfaces         IWifiStation  INetworkInterface  IDnsResolver, ITcpSocket, ITcpListener, IUdpSocket
                   IWifiAccessPoint                                       IByteStorage (storage)
                        |               |               |               |
platforms          EspWifi       EspEthernet       EspDnsResolver, EspTcpSocket, EspTcpListener,
                                                   EspUdpSocket, EspNvsStorage            (ESP32 family)
                   Esp8266Wifi                     Esp8266DnsResolver, Esp8266TcpSocket, Esp8266TcpListener,
                                                   Esp8266UdpSocket, Esp8266EepromStorage (ESP8266)
                   PcWifi        PcHostNetwork     PcDnsResolver, PcTcpSocket, PcTcpListener,
                                                   PcUdpSocket, PcByteStorage             (PC simulator)
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
| `ITcpListener` | A TCP server port on every interface: `listen()`, then `accept()` hands out connected `ITcpSocket`s the listener owns (a few at a time); closing one gives its room back. |
| `IUdpSocket` | A UDP socket bound to a local port: `open()`, `receive_from()` (`no_data` while nothing waits), `send_to()`, `close()`. |

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
  list on the screen,
- takes new networks while running (`set_networks()`, e.g. from a setup page):
  it leaves the current network and joins one of the new ones, keeping the
  hotspot open until that works; with none the hotspot opens at once.

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

`WifiSetupPortal` is a device's Wi-Fi setup page. It serves, through
`HttpServer` (one connection at a time, request and response in fixed
buffers, never blocking), a page that lists the networks of the last scan and
takes a network (or a hidden one typed in) and its password. The choice goes
to a `WifiCredentialStore` and the manager joins it (`set_networks()`);
"Forget" stores "no network" and opens the hotspot again (also
`forget_network()`, e.g. for a button held down). The page is served on the
hotspot and at the device's address in the joined network. While the hotspot
is up, `CaptiveDns` answers every DNS name with the hotspot's address and the
server sends any request for a name there: a phone that joins the hotspot
opens the page by itself (captive portal). `WifiCredentialStore` keeps one
checked record (SSID, password, hidden; or "no network") in any
`IByteStorage`, so the chosen network survives a restart; an application uses
it in place of its built-in network when a record exists.

```cpp
static EspTcpListener listener;            // platform objects in the target entry point
static EspUdpSocket   dns_socket;
static EspNvsStorage  settings("brick", "wifi");

WifiCredentialStore store(settings);
const WifiCredentials* saved = is_ok(store.load()) ? store.network() : nullptr;  // nullptr: forgotten or none
// ... WifiManager started with the saved network (or the built-in one) and a hotspot
WifiSetupPortal portal(wifi_manager, store, station, access_point, listener, &dns_socket, time, logger);
portal.start({ "DEVICE-SETUP", 80 });      // then portal.update() in the loop
```

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
`sdkconfig.defaults` (see the network test applications). `EspWifi::begin()`
starts `esp_hosted` (`esp_hosted_init()`): its own start-up constructor sits
in an object file nothing refers to, which the linker leaves out of the
static library. The ESP32-C6 on the Guition panels ships with esp_hosted
2.3.0 (the host component is 2.12): it lacks reading scan results one by one,
so `EspWifiStation` then reads the whole list (up to 20 access points), and
it may join a network it stored earlier by itself before the application
asks. The JC8012's ESP32-C6 ships with esp_hosted 2.1.0, which also ignores
the hotspot settings: it answers `ESP_OK` and keeps its own open hotspot
`ESP_xxxxxx`. `EspWifiAccessPoint::start()` reads the settings back, closes
such a hotspot and returns `Status::not_supported` (later calls at once), so
no open hotspot with a setup page stays on the air; that board can be set up
only from the build (or its own address in a network) until the ESP32-C6
firmware is updated.

`EspEthernet` drives the internal EMAC with an RMII PHY (IP101, RTL8201,
LAN87xx, DP83848, KSZ80xx or generic); the board profile supplies the pins.
`begin()` returns at once; the link follows the cable.

`EspDnsResolver` starts lwIP lookups in the TCP/IP thread
(`esp_netif_tcpip_exec`) and gets the answer there; `EspTcpSocket`,
`EspTcpListener` and `EspUdpSocket` are non-blocking lwIP sockets. The
hotspot's DHCP server offers the device itself as the DNS server (for
`CaptiveDns`). `EspNvsStorage` is an `IByteStorage` kept as one NVS blob
(started by `EspWifi::begin()`).

### ESP8266 (Arduino)

`Esp8266Wifi` with `Esp8266WifiStation` and `Esp8266WifiAccessPoint` over the
ESP8266WiFi library (asynchronous scan, `WiFi.begin()` without waiting, no
`String`). `Esp8266DnsResolver`, `Esp8266TcpSocket`, `Esp8266TcpListener`
and `Esp8266UdpSocket` use the raw lwIP API, which the ESP8266 runs in the
same cooperative context as the loop. `Esp8266EepromStorage` is an
`IByteStorage` over the EEPROM library (the flash sector the core reserves).
The application lists `ESP8266WiFi` (and `EEPROM`) in `lib_deps`. Code built
outside `src/` must end up in objects named `*.cpp.o`: the ESP8266 linker
script puts only those into flash, anything else fills the 32 KB of IRAM
(see `tools/network_app_build.py` in the test applications).

### PC simulator

`SimulatorBoard::wifi()` is a simulated radio (`PcWifi`): in simulated time a
scan finds the networks given with `--wifi SSID[:PASS[:RSSI]]` (default
"BRICK-LAB" / "brick-lab-pass", a neighbour and an open network) and joining
succeeds only with the right password, so headless runs are deterministic.
`SimulatorBoard::ethernet()` is the PC's own connection (`PcHostNetwork`).
`PcDnsResolver` and `PcTcpSocket` use the real network; with `--offline` they
fail at once, which keeps CI independent of the internet. `PcTcpListener` and
`PcUdpSocket` listen on 127.0.0.1 (or every interface); the listener can map
a device's port (80) to another one, so the setup page of a simulated device
opens at `http://localhost:8081/`. `PcByteStorage` keeps bytes in memory.

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
- One stored network (`WifiCredentialStore`); the password is stored in plain
  text (NVS, EEPROM), as ESP-IDF and Arduino store theirs.
- The setup page is plain HTTP without a login: anyone who joins the hotspot
  (WPA2 password) or the same network can change the network.
- On the ESP32-P4 panels the ESP32-C6 uses SDMMC slot 1 and the SD card
  slot 0 of the same controller; Wi-Fi and the SD card together are not
  tested yet.
