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
- przycisk symulatora (strona web, spacja w oknie, `--press`) tylko dla
  płytek, które go mają: `SimulatorBoardConfig::button`, ustawione dla
  ESP-12F ST7789,
- opis symulatora PC i pisania aplikacji pod niego (`docs/pc-simulator.md`),
- płytki: flaga `wifi` (CYD, Panel 480, JC1060, JC8012, ESP-12F) i `ethernet`
  (JC1060, IP101), domyślnie wyłączone; `SimulatorBoard` ma symulowane
  Wi-Fi (`--wifi`) i sieć komputera jako Ethernet (`--offline`),
- `IDisplayDevice::fill_rect()`: wypełnienie kolorem bez bufora pikseli
  (DMA/PPA), z którego korzysta `Screen::fill_area()`; mają je ILI9341
  (jeden przygotowany bufor DMA wysyłany wielokrotnie), MIPI DSI (wypełnienie
  PPA), ST7789 na ESP8266 (`fillRect` TFT_eSPI), SSD1963 i symulator PC,
- `brick/core/display/Rgb565Pixels.h`: `fill_rgb565()` i
  `copy_rgb565_swapped()` przetwarzają dwa piksele na słowo 32-bit,
- `MipiDsiDisplay` jest `IFrameBufferDisplay` (przy obrocie 0): LVGL lub
  aplikacja rysuje wprost w bufory skanowania, bez kopiowania,
- `LvglDisplayAdapter::create_framebuffer()` przyjmuje też tryb FULL,
- `St7701sRgbPanelConfig::bounce_buffer_lines`: liczba linii buforów bounce
  przy dowolnej liczbie framebufferów; 0 wyłącza je (DMA czyta PSRAM bez
  pracy CPU), a profil dwubuforowy panelu 480 ma 0; aplikacja, która czyta
  flash lub PSRAM pełną prędkością, włącza je, żeby obraz się nie przesuwał,
- `IFrameBufferDisplay::copy_to_frame_buffer()` i
  `copy_between_frame_buffers()` (domyślnie `not_supported`): kopia pikseli
  do bufora skanowania i między buforami, także do bufora poza ekranem;
  MIPI DSI kopiuje przez PPA w kolejce (do 8 kopii, wywołanie wraca po
  zleceniu, `wait_for_transfer_complete()` czeka na wszystkie), ST7701S
  i symulator PC przez CPU (`brick/core/display/FrameBufferCopy.h`),
- `LvglDisplayAdapter::create_buffered()`: rysowanie bez rwania z szybkiej
  pamięci: LVGL rysuje w trybie PARTIAL w bufory w RAM, adapter kopiuje
  obszary do bufora poza ekranem i pokazuje go page flipem raz na klatkę;
  przed klatką dokopiowuje z bufora na ekranie to, co zmieniła poprzednia
  klatka, a ta nie rysuje (jak synchronizacja LVGL w trybie DIRECT,
  `lv_area_diff`); JC8012 800x1280 z buforami 2 × 80 linii w SRAM: demo
  benchmarku średnio 30 fps zamiast 23 fps w trybie DIRECT (widżety 52 fps
  zamiast 26, obrazy 48 zamiast 26; 60 minut bez zawieszenia PPA),
- strona konfiguracji Wi-Fi (`docs/network.md`): `WifiSetupPortal` pokazuje
  sieci z ostatniego skanu, przyjmuje sieć (także ukrytą) i hasło, zapisuje
  je i łączy się; „Forget” (albo `forget_network()`, np. przycisk
  przytrzymany) zapisuje „brak sieci” i otwiera hotspot; strona działa na
  hotspocie i pod adresem urządzenia w sieci, a na hotspocie `CaptiveDns`
  kieruje każdą nazwę na urządzenie, więc telefon sam ją otwiera; w core
  także `HttpServer` (jedno połączenie naraz, stałe bufory),
  `WifiCredentialStore` (jeden sprawdzany rekord w `IByteStorage`) i
  `WifiManager::set_networks()` (zmiana sieci w trakcie pracy, hotspot
  zostaje do połączenia); interfejsy `ITcpListener` i `IUdpSocket`; ESP32:
  `EspTcpListener`, `EspUdpSocket`, `EspNvsStorage`, a DHCP hotspotu podaje
  urządzenie jako serwer DNS; ESP8266: `Esp8266TcpListener`,
  `Esp8266UdpSocket`, `Esp8266EepromStorage`; PC: `PcTcpListener` (port
  urządzenia można przenieść, np. 80 na 8081), `PcUdpSocket`,
- odtwarzanie dźwięku strumieniem PCM 16-bit (`docs/audio.md`): interfejs
  `IPcmOutput` (zapis bez czekania, głośność 0–100%); w core `ToneSource`
  (sinus/prostokąt z wygaszaniem 5 ms), `WavSource` (WAV 8/16-bit mono/stereo
  czytany kawałkami z dowolnego `IByteStorage`, np. `MemoryBytes` z pliku
  wkompilowanego w firmware) i `PcmPlayer` (dokłada próbki w `update()`);
  ESP32: `I2sPcmOutput` (I2S z kodekiem albo wzmacniaczem I2S, sterowanie
  wzmacniaczem z GPIO), `Es8311Codec` (głośność 0,5 dB na krok, 100% to
  -10 dB, bo głośniej przesterowuje NS4150) przez `LegacyI2cRegisters` albo
  `MasterI2cRegisters`, `DacPcmOutput` (8-bitowy DAC ESP32 z DMA w pierścieniu,
  zegar APLL, łagodny start i stop bez pyknięcia); `audio()` na płytkach
  JC1060, JC8012 (ES8311 + NS4150), ESP32-S3 480x480 (wzmacniacz I2S) i CYD
  (DAC, GPIO26) po włączeniu `audio = true`; PC: `PcPcmOutput` (liczy ramki
  w tempie częstotliwości próbkowania, bez dźwięku),

### Changed (wydajność grafiki, bez zmiany API)

- ILI9341: `draw_buffer()` wraca po skopiowaniu pikseli do buforów DMA,
  ostatnie transfery idą w tle (kolejna operacja na nie czeka); wiersze
  z paddingiem są pakowane w pełne transfery 4 KB; zamiana bajtów słowami
  32-bit,
- MIPI DSI: pełna klatka jest kopiowana do ukrytego bufora przez PPA (2D-DMA)
  zamiast `memcpy` CPU, a klatka narysowana w buforze skanowania jest
  pokazywana bez kopii; przy obrocie PPA obraca tylko rysowany obszar, prosto
  do bufora skanowania (bez dwóch dodatkowych buforów pełnoekranowych
  w PSRAM, CPU nie obraca całego ekranu przy każdej zmianie); po częściowym
  transferze sterownik czeka tylko na koniec DMA, nie na kolejne odświeżenie;
  konstruktor nie potrzebuje już `IMemoryProvider`; `dma_alignment_bytes` to
  linia cache L2 (było stałe 64); JC1060 1024x600: `fill_area()` całego ekranu
  429 → 3,5 ms, 15 pasów z SRAM 118 → 2,9 ms (CPU 98% → 23%), LVGL PARTIAL
  pełna klatka 330 → 41 ms, kopia pełnej klatki z PSRAM bez udziału CPU,
- JC1060: zegar pikseli 52 MHz (odświeżanie 59 Hz zamiast 45 Hz),
- `LvglDisplayAdapter`: flush jest asynchroniczny (`flush_wait_cb` LVGL):
  przy dwóch buforach LVGL renderuje kolejny obszar, gdy DMA wysyła
  poprzedni; w trybie DIRECT z buforami skanowania jeden page flip na klatkę,
  a czekanie na VSYNC dopiero przed następnym rysowaniem (ESP32-S3 480x480:
  klatka z czterema zmienionymi obszarami 94 → 23,5 ms),
- `AssetStreamer::stream()` przy wyświetlaczu asynchronicznym czyta kolejny
  pas do drugiej połowy bufora, gdy poprzedni jest wysyłany;
  `stream_to_buffer()` czyta upakowane wiersze wprost do bufora docelowego
  (bez kopii przez scratch),
- `Screen::fill()` zapisuje dwa piksele naraz (ESP32-S3: ×2 wobec `-O2`),
  `Screen::draw_text()` przycina glif raz i odwiedza tylko zapalone piksele
  (kolejny bit jedną instrukcją, NSAU/CLZ), zamiast sprawdzać każdy piksel
  (ESP32-S3: ×1,7 wobec `-O2`),
- `PartitionAssetSource` mapuje partycję w `begin()` i czyta assety przez
  cache flasha (`memcpy`) zamiast `esp_partition_read()`, który przesyła
  najwyżej 64 bajty na komendę i wstrzymuje cache obu rdzeni; bez miejsca
  w MMU zostaje `esp_partition_read()`,
- SSD1963: piksele idą jedną serią (CS i RS ustawione raz), trzy zapisy
  portu na bajt zamiast sześciu,
- ST7701S: `wait_for_transfer_complete()` bez semafora (esp_lcd kopiuje
  synchronicznie),
- ST7789 (ESP8266): własne wysyłanie pikseli zamiast `pushImage()` TFT_eSPI;
  `draw_buffer()` wypełnia jedną połowę bufora HSPI, gdy druga jest wysyłana,
  i zamienia bajty słowami 32-bit; `submit_buffer()` wraca po pierwszych
  64 bajtach, a resztę wysyła przerwanie HSPI (`async_transfer`), więc
  wywołujący przygotowuje w tym czasie kolejny bufor (bufor w DRAM,
  wyrównany do słowa; inny idzie od razu jak w `draw_buffer()`); ESP-12F
  240x240, SPI 40 MHz: benchmark SPI 15,9 → 31,8 fps (z flashem QIO
  80 MHz), obraz z flasha przez `AssetStreamer` 62,3 → 30,5 ms,

### Fixed

- ESP32-P4 (JC1060, JC8012): Wi-Fi przez ESP32-C6 nie startowało
  („Transport not initialized”): konstruktor esp_hosted leży w pliku
  obiektowym, do którego nic się nie odwołuje, więc linker go pomijał;
  `EspWifi::begin()` wywołuje teraz `esp_hosted_init()`; przy firmware C6
  2.3.0 (fabryczny na płytkach Guition) skan był zawsze pusty, bo C6 nie zna
  odczytu wyników po jednym rekordzie: `EspWifiStation` czyta wtedy całą
  listę,
- ESP32-P4 JC8012: firmware C6 2.1.0 ignoruje ustawienia hotspotu (odpowiada
  OK i zostawia własny, otwarty hotspot `ESP_xxxxxx`); `EspWifiAccessPoint`
  sprawdza teraz ustawienia po zapisie, taki hotspot zamyka i zwraca
  `not_supported`, zamiast zostawić otwartą stronę konfiguracji,
- ILI9341: oczekiwanie na transfer SPI ma limit czasu, a kolejkowane
  deskryptory SPI nie leżą już na stosie (po błędzie DMA czytał nadpisaną pamięć),
- GT911: flaga nowej próbki jest czyszczona po odczycie punktów,
- MIPI DSI: flaga odświeżenia współdzielona z przerwaniem jest atomowa,
- ST7701S: po page flipie `wait_for_vsync()` mógł wrócić od razu przez VSYNC
  sprzed flipu, a rysowanie trafiało do bufora wciąż na ekranie,
- ST7701S: flip pod koniec klatki (DMA czyta z wyprzedzeniem i minęło już
  koniec bufora) wchodzi dopiero klatkę później, a stary bufor jest wysyłany
  jeszcze raz; `wait_for_vsync()` czekał na jeden VSYNC i LVGL w trybie DIRECT
  rysował w buforze na ekranie (artefakty w demo benchmarku); po takim flipie
  czeka teraz dwa VSYNC,
- MIPI DSI: przy obrocie PPA czasem nie kończy kopii obszaru (na JC8012
  przy 90° z obszarami LVGL od kilkudziesięciu do kilkuset pikseli; esp_lcd
  przyjmuje je bez błędu), a blokujące wywołanie czekało bez końca: demo
  benchmarku w trybie PARTIAL stawało po kilku do kilkuset klatkach; sterownik
  czeka teraz z limitem 200 ms, loguje taki obszar i do końca spóźnionej kopii
  kopiuje przez CPU,
- MIPI DSI: PPA zawieszało się na stałe także przy obrocie 0, na niektórych
  blokach: obejście DIG-734 w ESP-IDF (do 5.5.5) obejmuje tylko blok, którego
  ostatnia jednostka (64 × 16 pikseli) ma mniej niż 1536 bitów, a na JC8012
  (rev 1.3) stawały też 198x256, 198x80 (6 × 16 pikseli, 1536 bitów)
  i 135x127 (7 × 15, 1680 bitów); po kilku do kilkunastu minutach pracy
  wszystkie kopie szły przez CPU (demo benchmarku: 31 → 18 fps); sterownik
  dzieli teraz każdy blok z ostatnią jednostką do 2048 bitów na pasy, dla
  których ESP-IDF włącza obejście (przy obrocie 90/270 jak dotąd: pasy,
  dla których jego decyzja jest właściwa); timeout przy pełnej kolejce kopii
  też jest logowany, z rozmiarem i adresami kopii, która utknęła,
- adapter LVGL w trybie DIRECT pokazywał bufor i czekał na VSYNC po każdym
  zmienionym obszarze, a nie raz na klatkę,
- `AssetStreamer` czyta assety z paddingiem wierszy (stride większy niż
  szerokość) wiersz po wierszu zamiast z przesunięciem,
- `FreeRtosTime::delay_ms()`: przy domyślnym takcie 100 Hz opóźnienie 1–9 ms
  dawało 0 taktów i nie usypiało zadania; pętla z `delay_ms(1)` zajmowała
  rdzeń w 100% i głodziła zadanie IDLE (ostrzeżenia task watchdog na CYD);
  każde niezerowe opóźnienie śpi teraz co najmniej jeden takt, a `millis()`
  liczy z `esp_timer` (dokładnie przy każdym `CONFIG_FREERTOS_HZ`),
- `PartitionAssetSource(const esp_partition_t*)`: `begin()` nie zastępuje już
  podanej partycji pierwszą partycją danych (szukał po pustej etykiecie),
  a odczyt za końcem partycji jest odrzucany także przy dużym przesunięciu,
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
