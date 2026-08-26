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

Pierwszy adapter BRICK dla LVGL v9 znajduje się w
`platforms/esp32/.../LvglDisplayAdapter`. Obsługuje ścieżkę PARTIAL/FULL:
prostokąt LVGL staje się `DisplayRect`, mapa pikseli — `PixelBuffer`, a
`lv_display_flush_ready()` jest wywoływane po zakończeniu `submit_buffer()` i
`wait_for_transfer_complete()`. Adapter nie alokuje buforów; otrzymuje je od
aplikacji. Tryb DIRECT z page-flipem pozostaje osobnym adapterem, ponieważ
wymaga jawnego powiązania buforów LVGL z `IFrameBufferDisplay`.

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
- ESP8266/ESP-12F + ST7789 240×240 + przycisk: obraz i wejście działają.

Panel CYD/ILI9341 oraz ESP32-P4/MIPI pozostają testami późniejszymi. Profile
ST7701S i ST7789 zwracają już capabilities odpowiadające ich rzeczywistym
interfejsom, zamiast odziedziczonego profilu `host`.

To nie jest jeszcze pełny transfer asynchroniczny: obecne sterowniki SPI używają
transakcji pollingowych, a ST7701S kopiuje prostokąt do framebufferu w trakcie
`esp_lcd_panel_draw_bitmap()`. ST7701S ma już rzeczywiste oczekiwanie na VSYNC.
Nie oznaczamy go jeszcze jako `async_transfer`, ponieważ callback
`on_color_trans_done` nie daje tu dodatkowej własności bufora względem
synchronicznego powrotu z funkcji.

ST7701S obsługuje teraz konfigurowalny tryb bezpośredniego framebufferu z maksymalnie
trzema buforami. Przy więcej niż jednym buforze sterownik wyłącza bounce buffer,
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
