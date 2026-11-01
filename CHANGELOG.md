# Changelog

Wszystkie istotne zmiany w BRICK Framework są opisane w tym pliku.

## [0.3.0] - kandydat 0.3.0-rc.2 (2026-11-01), przed testem sprzętowym

### Changed (niekompatybilne z 0.2.x)

- operacje, które mogą się nie udać, zwracają `brick::Status` zamiast `bool`
  (interfejsy, sterowniki, płytki, core); kontrakt aplikacji to
  `Status initialize(); void update();`,
- `IFileSystem` nie alokuje: `list_files()` wypełnia tablicę wywołującego,
  `open()` wiąże `FileHandle` z jednym z `kMaxOpenFiles` plików,
- `AudioBuffer` i `WavDecoder` wskazują na próbki wywołującego,
  `Screen::create_canvas()` rysuje w pikselach wywołującego,
- konstruktor `MipiDsiDisplay` przyjmuje `IMemoryProvider`,
- `Stm32UsbHost::read_blocks()` / `write_blocks()` zwracają `Status`,
- sterowniki STM32F1 biorą całe okablowanie z konfiguracji (`GpioPin`,
  instancje peryferiów, timingi panelu SSD1963) i nie mają domyślnych pinów
  żadnej płytki; `SpiNorFlash` implementuje `INorFlash`,
- `Stm32UsbFileSystem` jest `IRemovableFileSystem`, a `mount()` nie czeka
  na urządzenie (zwraca `not_ready` / `busy`),
- `IRemovableFileSystem::probe()` przyjmuje też katalog,
- definicje płytek z repozytorium `brick-boards` są częścią frameworka (`boards/`).

### Added

- platforma PC: symulator z oknem SDL2 lub bez okna (testy w CI),
  `SimulatorBoard`, mocki `BRICK::Mocks`,
- `IMemoryProvider` i `MemoryBlock`, `ISerialPort`, `IRemovableFileSystem`,
  `PcFileAssetSource`, przenośne adaptery LVGL w `libs/lvgl`,
- `DragSlider`, profile kolorów RGB565 i interfejs podświetlenia,
- `SerialLogger` i `NullLogger`, `BeepSequence` (sygnalizacja piknięciami
  bez blokowania), `Screen::fill_area()`, interfejs `INorFlash`,
- STM32F1: `Stm32I2cBus`, `Stm32SpiBus`, `Stm32Uart`, `Stm32HalTime`,
- symulator PC: buzzer, EEPROM i flash NOR w `SimulatorBoard`,
- płytka ESP-12F ST7789 (ESP8266), karta SD i podświetlenie CYD, dotyk
  i obrót DMA2D dla JC8012,
- adaptery STM32F1: SSD1963, dotyk rezystancyjny i FT5x06, EEPROM I2C,
  SPI NOR, buzzer, podświetlenie PWM, host USB MSC z FatFs,
- zasady pisania kodu embedded (`docs/embedded-coding-rules.md`) i ich
  sprawdzanie w CI (`libs/rules/brick_rules.py --strict`),
- sieć (`docs/network.md`): interfejsy `INetworkInterface`, `IWifiStation`,
  `IWifiAccessPoint`, `IDnsResolver`, `ITcpSocket`; przenośne `WifiManager`
  (najsilniejsza znana sieć, ponawianie z rosnącą przerwą, hotspot zapasowy),
  `NetworkSelector` (wspólny interfejs dostępu do internetu, np. Ethernet
  przed Wi-Fi) i `HttpProbe` (sprawdzenie internetu),
- ESP32: `EspWifi` (stacja i hotspot; na ESP32-P4 przez koprocesor ESP32-C6,
  `esp_wifi_remote` + `esp_hosted`), `EspEthernet` (EMAC + PHY RMII),
  `EspDnsResolver`, `EspTcpSocket`; ESP8266: `Esp8266Wifi`,
  `Esp8266DnsResolver`, `Esp8266TcpSocket`,
- symulator PC w przeglądarce: `--web [PORT]` uruchamia wbudowany serwer
  HTTP + WebSocket (`WebScreen`), który pokazuje ekran na stronie i przyjmuje
  dotyk i przycisk z przeglądarki; `--web-lan` udostępnia go w sieci lokalnej,
- płytki: flaga `wifi` (CYD, Panel 480, JC1060, JC8012, ESP-12F) i `ethernet`
  (JC1060, IP101), domyślnie wyłączone; `SimulatorBoard` ma symulowane
  Wi-Fi (`--wifi`) i sieć komputera jako Ethernet (`--offline`),

### Fixed

- ILI9341: oczekiwanie na transfer SPI ma limit czasu, a kolejkowane
  deskryptory SPI nie leżą już na stosie (po błędzie DMA czytał nadpisaną pamięć),
- GT911: flaga nowej próbki jest czyszczona po odczycie punktów,
- MIPI DSI: flaga odświeżenia współdzielona z przerwaniem jest atomowa,
- host USB STM32: przełączanie VBUS po odłączeniu urządzenia nie używa
  niezdefiniowanego pinu zasilania,
- WAV bez bajtu wyrównania po ostatnim chunku jest akceptowany; BMP
  z uszkodzonymi wymiarami nie przepełnia obliczeń offsetów,
- adapter LVGL zachowuje błąd wysyłania obrazu (`flush_status()`),
- `Stm32PwmBacklight` implementuje `IBacklight`,
- porównania czasu są odporne na przepełnienie licznika milisekund,
- linia bazowa i przesunięcie glifów w generowanych fontach.

## [0.2.1] - 2026-08-21

### Fixed

- poprawiono kolejność bajtów RGB565 podczas transmisji do ILI9341,
- wyłączono nieobsługiwane wewnętrzne podciąganie dla wejściowych GPIO ESP32,
- ustabilizowano obsługę dotyku XPT2046 na profilach CYD.

### Tests

- zweryfikowano profile CYD z PSRAM i bez PSRAM,
- dodano testy assetów z oddzielnej partycji flash oraz benchmark pełnego framebufferu.

## [0.2.0] - 2026-08-17

### Added

- obsługę wyświetlaczy i dotyku dla paneli JC1060 1024x600 na ESP32-P4,
- profile ST7701S RGB 480x480 i GT911 dla ESP32-S3,
- profile CYD z ILI9341 i XPT2046 dla ESP32-WROOM-32,
- adaptery ST7789 i przycisku GPIO dla ESP8266,
- interfejsy wyświetlacza, dotyku, podświetlenia, audio i systemu plików,
- dekodery BMP/WAV oraz prezentację obrazów dla LVGL,
- zewnętrzne repozytorium aplikacji testowych `brick-test-apps`.

### Changed

- uporządkowano profile sprzętowe według platform i wariantów układów,
- profile CYD przeniesiono do `platforms/esp32/.../wroom32/profiles`.

### Documentation

- dodano dokumentację architektury i procesu wersjonowania,
- przykłady sprzętowe opisano w `examples/README.md` i przeniesiono do osobnego repozytorium.
