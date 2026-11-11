# Architektura obsługi wyświetlaczy

Status: propozycja do walidacji na istniejących aplikacjach.

## Cel

Wspólny model ma obsługiwać jednocześnie:

- małe panele SPI, gdzie nie mieści się pełny ekran w RAM (CYD, ESP-12F),
- panele RGB z kontrolerem skanującym pamięć (np. ST7701S),
- panele MIPI DSI z DMA i synchronizacją VSYNC (ESP32-P4),
- szybki strumień obrazów, gdzie ważniejsze są przepustowość i brak kopiowania
  niż funkcje widgetów,
- LVGL, bez uzależniania kodu przenośnego od LVGL.

Kontrakty publiczne pozostają w `libs/interfaces/include/brick/interfaces`,
implementacje sprzętowe w `platforms/`, a aplikacje i profile płytek poza
frameworkiem, w `brick-test-apps` lub projektach produktowych.

## Najważniejsze rozróżnienie

W dokumentacji i kodzie należy rozdzielać trzy rodzaje bufora:

| Pojęcie | Znaczenie | Typowy przypadek |
|---|---|---|
| `draw buffer` | Bufor, do którego renderer tworzy fragment obrazu | LVGL partial, pas linii SPI |
| `framebuffer` | Bufor zawierający cały obraz logiczny | renderer 2D, dekoder JPEG |
| `scanout buffer` | Bufor czytany bezpośrednio przez kontroler RGB/MIPI | ST7701S RGB, LTDC, część MIPI |

„Double buffering” oznacza dwie powierzchnie tego samego rodzaju. Dwa małe
bufory LVGL nie są tym samym co dwa pełne bufory ekranu. W szczególności
zewnętrzny kontroler SPI nie może wykonać bezpośredniego page-flip — dane trzeba
przesłać do kontrolera, zwykle prostokątem i przez DMA.

## Warstwy

### 1. Przenośne typy i kontrakty

Docelowy podział `interfaces/display`:

```text
DisplayTypes.h          PixelFormat, Rotation, RenderMode
DisplayRect.h           prostokąt x/y/w/h, clipping i walidacja
PixelBuffer.h           nieposiadający pamięci widok: ptr, w, h, stride, format
DisplayCapabilities.h   możliwości pamięci, DMA, VSYNC, scan-out i częściowego flush
IDisplayDevice.h        init, konfiguracja panelu, okno transferu
IDisplayPresenter.h     submit fragmentu/klatki, async completion, wait
```

Nie należy przenosić do tych nagłówków ESP-IDF, FreeRTOS, LVGL ani alokatora.
`PixelBuffer` jest widokiem, a nie właścicielem pamięci. Własność buforów
pozostaje po stronie aplikacji, puli albo sterownika panelu.

Kontrakt `IDisplayDevice` opiera się wyłącznie o `PixelBuffer` + `DisplayRect`.
Surowy interfejs z osobnymi argumentami szerokości, wysokości i `byte_count`
został odrzucony, ponieważ nie opisywał stride ani formatu danych.

### 2. Pula buforów

Pula powinna obsługiwać jawnie:

- `single`: jeden bufor, najmniejsze zużycie RAM;
- `double`: renderer zapisuje do jednego bufora, a DMA/scan-out używa drugiego;
- `triple`: trzeci bufor oddziela producenta, transfer i prezentację;
- `partial`: bufor ma tylko ustaloną liczbę wierszy lub prostokąt.

Pula nie może zakładać, że każdy backend potrafi zamienić pełne bufory. Dla SPI
zwraca bufory draw, natomiast dla RGB/MIPI może przejąć bufory dostarczone przez
sterownik kontrolera. Każdy bufor musi mieć jawne wymagania: wyrównanie, DMA,
PSRAM/internal RAM, stride i moment, od którego można go ponownie zapisać.

### 3. Backend panelu

Backend odpowiada tylko za sprzęt i transfer:

- ustawia okno adresowe lub adres scan-out,
- przyjmuje fragment albo pełną powierzchnię,
- wykonuje transfer synchronicznie lub asynchronicznie,
- zgłasza `transfer complete` i opcjonalnie `VSYNC`,
- nie zna widgetów ani modelu aplikacji.

To pozwala użyć tej samej ścieżki dla `Ili9341SpiDisplay`, `St7701sRgbDisplay`,
`MipiDsiDisplay`, przyszłego LTDC oraz emulatora PC.

## Profile sprzętowe

### SPI: CYD i stacja pogodowa

Domyślny tryb to `partial`: 1–kilkadziesiąt wierszy, dwa bufory DMA, kolejka
transferów i back-pressure. Pełny framebuffer jest opcjonalny i tylko wtedy,
gdy pozwala na to PSRAM. Dla streamingu należy dekodować lub otrzymywać obraz
bezpośrednio do bieżącego bufora i przesyłać go pasami; nie wolno wymagać
drugiej kopii pełnej klatki.

### RGB: ST7701S

Preferowany jest pełny RGB565 `scanout buffer` w PSRAM, double buffering i
zamiana adresu przy VSYNC. Renderowanie LVGL powinno używać `DIRECT` tylko przy
buforach ekranowych utrzymywanych w synchronizacji; w przeciwnym razie
`PARTIAL` z flush do backendu.

### MIPI DSI: ESP32-P4

Backend powinien udostępniać bufor dostarczony przez kontroler, asynchroniczny
transfer DMA oraz zakończenie skorelowane z VSYNC. JPEG/RGB565 streaming powinien
mieć osobną ścieżkę prezentacji, omijającą widgety LVGL, ale korzystającą z tej
samej puli i mechanizmu back-pressure.

### STM32/GD32 — później

Kontrakt nie powinien nazywać funkcji `esp_*`. Pierwszym przyszłym adapterem
może być STM32 LTDC/DMA2D, a następnie GD32 lub kontroler SPI. Nie dodajemy tych
platform do pierwszego pionowego wycinka, dopóki model nie zostanie sprawdzony
na ESP32.

## LVGL

Integracja powinna być cienkim adapterem:

```text
LVGL render -> flush(area, px_map)
             -> PixelBufferView
             -> IDisplayPresenter::submit(area, view)
             -> DMA/scan-out complete
             -> lv_display_flush_ready()
```

Adapter LVGL nie powinien posiadać własnej polityki alokacji ani kopiować
obrazu bez potrzeby. Konfiguracja renderowania wynika z capabilities backendu:

- SPI/CYD: `PARTIAL`, jeden lub dwa małe draw buffery;
- RGB/MIPI z pełnymi buforami: `DIRECT`/`FULL` tylko gdy sterownik gwarantuje
  synchronizację i właściwy page-flip;
- brak pewnego DMA: jeden bufor i jawne oczekiwanie na zakończenie flush.

Adapter BRICK dla LVGL v9 znajduje się w `libs/lvgl` (`LvglDisplayAdapter`).
W ścieżce PARTIAL/FULL prostokąt LVGL staje się `DisplayRect`, mapa pikseli —
`PixelBuffer`; flush tylko wywołuje `submit_buffer()`, a
`wait_for_transfer_complete()` wykonuje `flush_wait_cb`, gdy LVGL potrzebuje
bufora z powrotem. Adapter nie alokuje buforów; otrzymuje je od aplikacji.
`create_framebuffer()` wiąże bufory LVGL z `IFrameBufferDisplay` (DIRECT
albo FULL) i kończy klatkę page-flipem.

`create_buffered()` łączy szybką pamięć z brakiem rwania: LVGL rysuje
w trybie PARTIAL w dwa bufory w SRAM, adapter kopiuje każdy obszar
(`copy_to_frame_buffer()`) do bufora skanowania poza ekranem, a po ostatnim
obszarze klatki robi jeden page flip. Bufor poza ekranem jest o dwie klatki
starszy, więc przed pierwszym obszarem adapter dokopiowuje do niego z bufora
na ekranie (`copy_between_frame_buffers()`) to, co zmieniła poprzednia
klatka, minus obszary, które ta klatka i tak narysuje (`lv_area_diff`, jak
synchronizacja LVGL w trybie DIRECT). Dokopiowanie całych obszarów zamiast
różnicy dawało widoczne przycięcia w scenie prostokątów. Ponad 32 obszary
(`LV_INV_BUF_SIZE`) LVGL zamienia na odświeżenie całego ekranu. Kopie są
w tle: MIPI DSI kolejkuje je w PPA (do 8 zleceń), ST7701S i symulator PC
kopiują przez CPU.

LVGL rozróżnia partial, direct i full render mode, a dwa bufory partial mają
sense wtedy, gdy transfer jest wykonywany w tle. To jest dokładnie powód, dla
którego `flush ready` musi być częścią adaptera, a nie ukrytą funkcją
`draw_buffer()`.

## Streaming

Streaming obrazu i UI LVGL powinny być dwoma konsumentami tego samego backendu,
ale nie jedną klasą „GraphicsEngine”.

```text
JPEG/RGB565 source -> decoder -> BufferPool -> async presenter -> panel
LVGL              -> flush area ------------------------------^ 
```

Wspólne elementy to format, stride, prostokąt, własność bufora, kolejność i
sygnał zakończenia. Stream powinien móc odrzucić starą klatkę, gdy panel lub
DMA nie nadąża; dla UI LVGL należy zachować semantykę kolejki i nie nadpisywać
bufora przed `flush_ready`.

## Assety

Narzędzia powinny być osobnym modułem narzędziowym, a wygenerowane pliki
pozostawać w aplikacji:

```text
tools/assets/
  image_to_rgb565.py       PNG/BMP/JPEG -> raw/header/LVGL asset
  font_to_bitmap.py        TTF/OTF -> bitmap font/header
  inspect_asset.py         rozmiar, format, stride, checksum

brick-test-apps/<app>/assets/generated/
```

Parametry muszą obejmować format, endianess RGB565, crop/scale, alpha,
wyrównanie DMA, kompresję i zestaw znaków fontu. Domyślnie narzędzia nie
powinny generować gigantycznych tablic w `libs/core`; asset jest własnością
konkretnej aplikacji. Należy wykorzystać istniejące przypadki
`BmpDecoder`, `convert_images.py`, generatorów fontów i nagłówków LVGL jako
fixtures testowe, zamiast od razu przepisywać je wszystkie.

## Kolejność wdrożenia

### Stan bieżący

Pierwszy kompatybilny wycinek jest już w repozytorium:

- `DisplayRect`, `PixelBuffer` i `DisplayCapabilities` są host-testowalnymi
  typami w `interfaces/display`;
- `IDisplayDevice::draw_buffer()` jest synchronicznym kontraktem transferu obrazu;
- `submit_buffer()`, `wait_for_transfer_complete()` i `wait_for_vsync()` tworzą
  jawne punkty rozszerzenia dla sterowników asynchronicznych;
- opcjonalny `IFrameBufferDisplay` pozwala sterownikowi udostępnić bezpośrednie
  framebuffer-y i operację `present_frame_buffer()` bez kopiowania pełnego ekranu;
- `Ili9341SpiDisplay` opisuje możliwości SPI oraz obsługuje wiersze z paddingiem
  stride bez tworzenia pełnej kopii obrazu.

### Walidacja sprzętowa

Potwierdzone na dostępnych urządzeniach:

- ESP32-S3 + ST7701S RGB 480×480 + GT911: obraz i dotyk działają;
- ESP8266/ESP-12F + ST7789 240×240 + przycisk: obraz i wejście działają;
- CYD (ESP32 + ILI9341 + XPT2046, z PSRAM i bez): obraz, assety, LVGL
  i dotyk działają (2026-11);
- ESP32-P4 JC1060 (MIPI DSI 1024×600 + GT911): framebuffer, assety, LVGL
  FULL/PARTIAL/DIRECT, PPA i dotyk działają (2026-11);
- ESP32-P4 JC8012 (MIPI DSI 800×1280 + GSL3680): framebuffer, assety, LVGL,
  obrót 0/90/180/270° przez PPA (obraz i dotyk) i demo benchmarku działają
  (2026-11).

Profile
ST7701S i ST7789 zwracają już capabilities odpowiadające ich rzeczywistym
interfejsom, zamiast odziedziczonego profilu `host`.

ST7701S kopiuje prostokąt do framebufferu w trakcie
`esp_lcd_panel_draw_bitmap()`, więc nie jest oznaczony jako `async_transfer`
(bufor wywołującego jest wolny po powrocie). Ma rzeczywiste oczekiwanie na
VSYNC; `present_frame_buffer()` kasuje VSYNC sprzed flipu, żeby
`wait_for_vsync()` czekał na przełączenie. esp_lcd przepina DMA na nowy bufor
na końcu wysyłanego, a DMA czyta z wyprzedzeniem: flip w wygaszaniu przed
VSYNC (plus kilka linii) wchodzi klatkę później, więc wtedy `wait_for_vsync()`
czeka na drugi VSYNC. Bez tego LVGL w trybie DIRECT rysował czasem w buforze
na ekranie (widoczne w demo benchmarku na wykresie i prostokątach).

### Mniej pracy CPU przy przesyłaniu obrazu

Zasada: CPU nie kopiuje pikseli, jeśli może to zrobić DMA, i nie czeka, jeśli
może w tym czasie przygotować następny bufor.

| Sterownik | Co robi DMA | Co zostaje dla CPU |
|---|---|---|
| ILI9341 (CYD) | dwa bufory DMA 4 KB na przemian; `draw_buffer()` wraca, gdy ostatnie transfery jeszcze trwają | zamiana bajtów (słowami 32-bit) przy kopiowaniu do bufora DMA; `fill_rect()` bez kopiowania |
| MIPI DSI (P4) | pełna klatka: PPA kopiuje do ukrytego bufora i flip; obszar: 2D-DMA; obrót: PPA obraca tylko obszar; `fill_rect()`: wypełnienie PPA | nic przy rysowaniu wprost w bufor skanowania (`IFrameBufferDisplay`) |
| ST7701S (S3) | skanowanie z PSRAM; z buforami bounce (`bounce_buffer_lines`, 0 = bez, tak ma profil dwubuforowy) wypełnia je CPU w przerwaniu | kopiowanie obszaru do framebufferu (esp_lcd) |
| ST7789 (ESP8266) | brak DMA SPI; `submit_buffer()`: przerwanie HSPI ładuje kolejne 64 bajty, `draw_buffer()`: jedna połowa bufora HSPI wysyłana, druga ładowana | ładowanie bufora HSPI (zamiana bajtów słowami 32-bit); `fill_rect()` przez `fillRect` TFT_eSPI |
| SSD1963 (STM32F1) | brak (GPIO) | seria bajtów: trzy zapisy portu na bajt |

Kontrakt pozostaje ten sam: `submit_buffer()` może wrócić przed końcem
transferu, a `wait_for_transfer_complete()` oddaje bufor wywołującemu.
`DisplayCapabilities::async_transfer` mówi, czy bufor jest w użyciu po
powrocie z `submit_buffer()`; sterownik, który kopiuje piksele do własnych
buforów (ILI9341), zgłasza `false`, bo bufor wywołującego jest wolny od razu.
Z tego korzystają:

- `LvglDisplayAdapter`: flush tylko zleca transfer, a czeka dopiero
  `flush_wait_cb`, gdy LVGL potrzebuje bufora; w trybie DIRECT jeden page flip
  na klatkę (po ostatnim obszarze), czekanie na VSYNC przed kolejnym
  rysowaniem; w trybie FULL z buforami skanowania czeka od razu, bo LVGL
  zaczyna następną klatkę w drugim buforze, który do flipu jest na ekranie,
- `AssetStreamer::stream()`: przy `async_transfer` dzieli bufor na dwie
  połowy i czyta kolejny pas, gdy poprzedni jest wysyłany,
- `Screen::fill_area()`: najpierw `fill_rect()` urządzenia, dopiero bez niego
  paski z pikseli wywołującego.

Assety z flasha (`PartitionAssetSource` na ESP32) są czytane przez mapowanie
partycji w przestrzeń adresową, czyli przez cache flasha: `read()` to
`memcpy`. `esp_partition_read()` przesyła najwyżej 64 bajty na komendę SPI
i na ten czas wstrzymuje cache obu rdzeni; zostaje tylko wtedy, gdy w MMU nie
ma miejsca na mapowanie. Przepustowość zależy dalej od trybu flasha (QIO
przesyła cztery bity na takt, DIO dwa).

#### ESP32-S3 z panelem RGB (pomiary na panelu 480x480, 2026-11)

Flash i PSRAM dzielą na S3 jedną magistralę (MSPI), z której DMA panelu
czyta framebuffer (480x480 RGB565 przy pclk 12 MHz to ok. 20 MB/s). Gdy
aplikacja czyta flash lub PSRAM pełną prędkością, DMA nie dostaje danych na
czas i obraz przesuwa się w bok, aż do restartu transmisji.

| Konfiguracja aplikacji | LVGL, pełna klatka | wypełnienie FB w PSRAM | assety z flasha |
|---|---|---|---|
| 160 MHz, flash DIO, cache 32 KB | 70,4 ms | 17,1 ms | 18,3 fps |
| 240 MHz, flash QIO | 58,9 ms | 16,6 ms | 25,9 fps |
| + cache danych 64 KB, linia 64 B | 47,1 ms | 11,9 ms | 32,8 fps, obraz się przesuwa |
| + bufory bounce (10 linii) | 47,4 ms | 13,0 ms | 30,4 fps, stabilnie |

Aplikacje testowe S3 mają 240 MHz, QIO i cache 64 KB z linią 64 B
(`sdkconfig.defaults`, `board_build.flash_mode = qio`). Bufory bounce
(`bounce_buffer_lines`) kosztują ok. 6–7% CPU (przerwanie kopiuje cały
ekran z PSRAM) i włącza je tylko aplikacja, która strumieniuje assety
z flasha bez przerwy. `CONFIG_LCD_RGB_RESTART_IN_VSYNC` pozostaje wyłączone,
bo restart DMA w każdym VBlank zaburzał page flipy.

#### ESP8266 (ESP-12F, ST7789 240x240 przez SPI 40 MHz; pomiary 2026-11)

HSPI nie ma DMA, więc CPU ładuje 64-bajtowy bufor W0..W15 przed każdą
komendą. Klatka 240x240 (115 KB) to przy 40 MHz 23,7 ms samego SPI
(wypełnienie jednym kolorem). Panel nie przyjmuje 80 MHz (brak obrazu),
a 60 MHz ESP8266 i tak daje jako 40 MHz (dzielnik całkowity z 80 MHz).
Assety leżą we flashu (PROGMEM) i są czytane przez cache 32 KB.

| Benchmark SPI (pasy z flasha, CPU 160 MHz) | kopia z flasha | wysyłanie | klatka |
|---|---|---|---|
| TFT_eSPI `pushImage()`, flash DOUT 40 MHz | 34 ms | 28 ms | 62 ms (15,9 fps) |
| własna pętla `draw_buffer()`, DOUT | 34,3 ms | 26,5 ms | 62 ms |
| własna pętla, flash QIO 80 MHz | 22,8 ms | 26,0 ms | 49 ms |
| `submit_buffer()` z przerwania, dwa półpasy, DOUT | nakłada się | | 44 ms (22,7 fps) |
| to samo, QIO 80 MHz | nakłada się | | 31,4 ms (31,8 fps) |

TFT_eSPI czeka na wysłanie każdych 32 bajtów, zanim załaduje kolejne
(przy zamianie bajtów — bajt po bajcie), i linia stoi w tym czasie. Własna
pętla `draw_buffer()` ładuje jedną połowę bufora (W0..W7 albo W8..W15,
`SPIUMOSIH`), gdy druga jest wysyłana. `submit_buffer()` wysyła pierwsze
64 bajty i wraca; przerwanie końca komendy HSPI (dzielone z SPI0) ładuje
następne, a CPU czyta w tym czasie kolejny pas z flasha. Przerwanie czyta
tylko bufor w DRAM, więc działa też, gdy zapis do flasha wyłącza cache.
Aplikacje ESP-12F mają flash QIO 80 MHz (`board_build.flash_mode = qio`,
`board_build.f_flash = 80000000L`; wcześniej DOUT 40 MHz). Wi-Fi (hotspot
i skanowanie) działa obok przerwania SPI.

#### ESP32 (CYD, ILI9341 przez SPI 80 MHz; pomiary na CYD z PSRAM, 2026-11)

Klasyczny ESP32 wykonuje kod i czyta stałe (fonty LVGL) z flasha przez
cache, więc taktowanie flasha ogranicza rysowanie bardziej niż sam CPU.

| Konfiguracja aplikacji | LVGL, pełna klatka | LVGL, zmiana widżetów | assety z flasha | klatka z PSRAM |
|---|---|---|---|---|
| repozytorium wcześniej: `-Og`, 160 MHz, flash DIO 40 MHz | 124 ms | 24,1 ms | 12,4 fps¹ | — |
| `-O2`, 240 MHz, flash DIO 40 MHz, PSRAM 40 MHz | 77,6 ms | 14,6 ms | 15,9 fps | 17,9 ms |
| + flash QIO 80 MHz, PSRAM 80 MHz | 43,8 ms | 8,7 ms | 28,6 fps | 16,0 ms (limit SPI) |

¹ stary framework przy 240 MHz.

`fill_area()` na cały ekran zajmuje CPU w 4% (DMA `fill_rect()`), rysowanie
pasami w 17% (kopia z zamianą bajtów, reszta w tle). Aplikacje testowe CYD
mają `-O2`, 240 MHz i flash QIO 80 MHz (`board_build.f_flash`,
`sdkconfig.defaults`), te z PSRAM także PSRAM 80 MHz; LVGL ze stosem 8 KB
(przy `-O2` zużywa 3,7 KB). Funkcji LVGL `LV_ATTRIBUTE_FAST_MEM` nie da się
przenieść do IRAM: brakuje 36 KB.

#### ESP32-P4 (JC1060, MIPI DSI 1024x600; pomiary 2026-11)

Panel skanuje bufor z PSRAM (1024x600 RGB565 przy 59 Hz to ok. 73 MB/s),
a CPU, PPA i 2D-DMA dzielą z nim przepustowość PSRAM. Kopia pełnej klatki
(1,2 MB) trwa ok. 25 ms tak samo przez PPA, jak przez `memcpy`, bo ogranicza
ją PSRAM; PPA zwalnia przy tym CPU. Stary sterownik po każdym częściowym
transferze czekał na koniec odświeżenia (22 ms), stąd największe różnice.

| Pomiar (`-O2`, 360 MHz) | stary framework | nowy | nowy + cache L2 256 KB, linia 128 B |
|---|---|---|---|
| `fill_area()` całego ekranu | 429 ms | 3,5 ms (CPU 2%) | 3,5 ms |
| 15 pasów 1024x40 z SRAM | 118 ms (CPU 98%) | 2,9 ms (CPU 23%) | 3,0 ms |
| wypełnienie klatki w PSRAM (CPU) | 17,0 ms | 16,9 ms | 7,9 ms |
| pełna klatka z PSRAM (kopia + flip) | 43,8 ms (CPU 56%) | 44,0 ms (CPU 0%) | 43,9 ms |
| LVGL PARTIAL (bufory SRAM), pełna klatka | 330 ms | 40,7 ms | 40,7 ms |
| LVGL PARTIAL, cztery etykiety | 88 ms | 3,4 ms | 3,4 ms |
| LVGL DIRECT w buforach skanowania, pełna klatka | — | 65,4 ms | 44,0 ms |
| LVGL DIRECT, zmiana widżetów | — | 22,1 ms (CPU 40%) | 22,0 ms (CPU 31%) |
| assety z flasha, pełne klatki | 5,1 fps | 8,3 fps (DIO), 11,4 fps (QIO) | — |

LVGL DIRECT pokazuje każdą zmianę przy najbliższym odświeżeniu, bez rwania
i przy małym obciążeniu CPU; PARTIAL rysuje szybciej, ale wprost w bufor na
ekranie. Cache L2 512 KB zostawia za mało wewnętrznego RAM (aplikacja nie
startuje), a sama linia 128 B przy 128 KB daje tylko 16,9 → 12,9 ms. Przy
zegarze pikseli 52 MHz (59 Hz, profil JC1060) flip trwa 16,5 ms zamiast 21 ms,
a kopia pełnej klatki z flipem 34 ms zamiast 44 ms; pełna klatka DIRECT
potrzebuje wtedy trzech odświeżeń (50 ms) zamiast dwóch.

Aplikacje testowe JC1060 mają cache L2 256 KB z linią 128 B i flash QIO
(`sdkconfig.defaults`, `board_build.flash_mode = qio`), a aplikacje LVGL także
rysowanie przez PPA (`LV_USE_PPA`, `LV_USE_PPA_IMG`, `LV_DRAW_BUF_ALIGN` równe
linii cache) i bufory wyrównane do `dma_alignment_bytes`. W tej konfiguracji
(z zegarem 52 MHz) benchmark LVGL w trybie FULL ma 29,6 fps w obu seriach
(repozytorium wcześniej: 18,2 i 15,2 fps), wypełnienie klatki 8,2 ms, assety
z flasha 14,5 fps (wcześniej 5,1), a z PSRAM 19,7 fps. Klatki wyrównują się do
odświeżeń: praca na klatkę między dwoma okresami 59 Hz (33,8 ms) a dwoma
okresami 45 Hz (44 ms) przy 52 MHz zajmuje trzy odświeżenia, stąd assety
z PSRAM przy 40 MHz miały 22,7 fps.

#### ESP32-P4 (JC8012, MIPI DSI 800x1280; pomiary 2026-11)

Klatka ma 2 MB, panel odświeża się z częstotliwością 52 Hz (zegar pikseli
60 MHz, 19,2 ms). Obrót o 90/180/270° robi PPA na rysowanym obszarze; stary
sterownik obracał na CPU i po każdym pasku czekał na odświeżenie.

| Pomiar (`-O2`, 360 MHz) | stary framework | nowy | nowy + cache L2 256 KB / 128 B, QIO |
|---|---|---|---|
| `fill_area()` całego ekranu | 715 ms | 5,8 ms (CPU 1%) | 5,8 ms |
| to samo przy obrocie 90° | 14,3 s (CPU 96%) | 5,8 ms | 5,8 ms |
| 32 pasy 800x40 z SRAM | 250 ms (CPU 99%) | 5,8 ms (CPU 24%) | 5,8 ms |
| to samo przy obrocie 90° | 14,2 s (CPU 97%) | 18,8 ms (CPU 6%) | 18,8 ms |
| wypełnienie klatki w PSRAM (CPU) | 33,3 ms | 33,2 ms | 15,7 ms |
| pełna klatka z PSRAM (kopia + flip) | 57 ms (CPU 82%) | 57 ms (CPU 0%) | 57 ms |
| LVGL PARTIAL, pełna klatka | 616 ms | 50,5 ms | 50,6 ms |
| LVGL PARTIAL, cztery etykiety | 77 ms | 4,1 ms | 4,1 ms |
| LVGL DIRECT, pełna klatka | — | 76 ms | 57 ms |
| LVGL FULL (aplikacja benchmarku) | 10,4 fps | 10,4 fps | 17,3 fps |
| assety z flasha, pełne klatki | 3,2 fps | 4,9 fps (DIO) | 7,4 fps |

Aplikacje testowe JC8012 mają tę samą konfigurację co JC1060 (cache L2
256 KB z linią 128 B, flash QIO, w aplikacjach LVGL `LV_USE_PPA`); zegar
pikseli pozostaje 60 MHz, bo przy klatce 2 MB szybsze skanowanie zabrałoby
przepustowość PSRAM. Aplikacja dotyku nie miała ustawień rewizji układu
(ESP-IDF 5.5 budował ją dla P4 v3 i nie startowała na v1.x) ani zależności
`memory.ld` od `sdkconfig.h`; obie są uzupełnione.

Demo benchmarku (`display_benchmark_demo`, 800x1280 bez obrotu):

| Scena | DIRECT (PSRAM) | SRAM + flip (`create_buffered()`, 2 × 80 linii) |
|---|---|---|
| Widżety | 26,0 fps | 52,0 fps |
| Tekst | 26,0 fps | 17,4 fps |
| Obrazy | 26,0 fps | 48,4 fps |
| Prostokąty | 23,2 fps | 25,0 fps |
| Pełny ekran | 26,0 fps | 26,0 fps |
| Mieszana | 10,4 fps | 10,8 fps |
| Średnio | 22,9 fps | 30,0 fps |

Rysowanie w PSRAM nie mieści się w jednym okresie odświeżania, w SRAM tak.
Tekst traci, bo ponad 32 etykiety wymuszają odświeżenie całego ekranu
(ok. 40 ms, trochę ponad dwa okresy, więc klatka czeka na trzecie
odświeżenie); pomóc mogłyby trzy bufory skanowania (sterownik DPI pozwala na
trzy). 40 linii zamiast 80 daje dwa razy więcej kopii PPA i mniej fps.

PPA (P4 v1.3) potrafi zawiesić się na stałe na pewnych wymiarach bloku
(błąd sprzętowy DIG-734). Blok jest dzielony na jednostki 64 piksele
wyjścia × 16 wierszy wejścia; gdy jest ich kilka, a ostatnia jest mała, PPA
musi ominąć kolejność makrobloków. ESP-IDF do 5.5.5 włącza to obejście tylko
dla ostatniej jednostki poniżej 1536 bitów, liczonej z nieobróconego bloku.
Zmierzone zawieszenia przy obrocie 0: 198x256 i 198x80 (6 × 16 pikseli =
1536 bitów) oraz 135x127 (7 × 15 = 1680 bitów), po kilku do kilkunastu
minutach demo (średnia spadała z 31 do 18-20 fps, bo kopie przejmował CPU).
`MipiDsiDisplay` dzieli teraz każdy blok, którego ostatnia jednostka ma do
2048 bitów, na pasy, dla których decyzja ESP-IDF jest właściwa (przy każdym
obrocie i w obu kopiach); 60 minut demo bez zawieszenia. Gdy kopia PPA nie
skończy się w 200 ms, sterownik loguje jej wymiary i adresy i kopiuje przez
CPU, dopóki PPA nie skończy.

ST7701S obsługuje teraz konfigurowalny tryb bezpośredniego framebufferu z maksymalnie
trzema buforami. Przy więcej niż jednym buforze sterownik
udostępnia pamięć przez `IFrameBufferDisplay` i przełącza skanowany bufor przez
`present_frame_buffer()`. Domyślnie pozostaje jeden bufor, aby nie zwiększać wymagań
pamięci istniejących aplikacji. Następna walidacja sprzętowa powinna uruchomić
wariant dwubuforowy na panelu 4-calowym i zmierzyć czas renderowania, transferu
oraz liczbę pominiętych klatek.

1. Dodać host-testowalne `DisplayRect`, `PixelBuffer` i `DisplayCapabilities`;
   nie zmieniając jeszcze sterowników sprzętowych.
2. Napisać mock prezentera mierzący prostokąty, stride, własność bufora i
   kolejność `submit/complete`.
3. Przenieść `Surface`, `BufferManager` i `IDisplayBackend` z
   `video-stream-test` do BRICK w mniejszych klasach i stylu BRICK
   (`I...`, osobne `.h/.cpp`, katalogi według odpowiedzialności).
4. Zaimplementować pierwszy adapter ESP32 SPI dla CYD oraz adapter RGB ST7701S;
   zweryfikować je w realnych aplikacjach, nie tylko w mocku.
5. Dodać adapter LVGL i porównać partial/direct/full na tych samych assetach.
6. Dopiero potem przenieść ścieżkę MIPI/JPEG z ESP32-P4 i uruchomić pomiary
   FPS, czasu renderowania, czasu DMA, dropped frames i kopiowania.
7. Na końcu dodać skrypty assetów jako niezależny, wersjonowany moduł narzędzi.

Pierwszym kryterium sukcesu nie jest maksymalna liczba klas, lecz możliwość
uruchomienia jednej aplikacji w dwóch trybach: LVGL partial dla ograniczonego
RAM oraz szybki stream bez pełnej kopii ekranu.
