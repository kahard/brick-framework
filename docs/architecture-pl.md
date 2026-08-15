# BRICK Framework

**BRICK** means **Building Reusable Interfaces, Components and Kits**.

It is a modular C++ framework for building portable embedded software across
different hardware platforms. The name emphasizes the intended way of using
the project: applications are assembled from reusable building blocks, while
platform-specific code remains isolated in adapters.

**BRICK** to modularny zestaw bibliotek C++ przeznaczony do budowy przenośnego oprogramowania embedded dla różnych platform sprzętowych.

Głównym celem projektu jest stworzenie wspólnej infrastruktury, którą można wykorzystywać w wielu niezależnych projektach bez silnego wiązania kodu aplikacyjnego i sterowników urządzeń z konkretnym mikrokontrolerem, SDK producenta lub systemem operacyjnym.

Projekt powinien wspierać przede wszystkim:

- STM32,
- GD32,
- ESP32,
- PC:
  - Windows,
  - Linux.

Wsparcie kolejnych platform, rodzin MCU i urządzeń powinno być dodawane dopiero wtedy, gdy pojawi się realna potrzeba.

---

# 1. Główne cele projektu

BRICK ma być zestawem wielokrotnie wykorzystywanych „klocków”, z których można budować kolejne projekty embedded.

Najważniejsze cele:

1. **Przenośność kodu**
   - możliwie duża część kodu powinna być niezależna od konkretnego MCU,
   - przejście między STM32, GD32, ESP32 lub implementacją PC nie powinno wymagać zmian w logice wyższego poziomu.

2. **Separacja sprzętu od logiki**
   - kod wykorzystujący GPIO, UART, SPI, I2C, ADC itd. nie powinien bezpośrednio zależeć od STM32 HAL, GD32 SDK, ESP-IDF itp.,
   - zależność od sprzętu powinna być zamknięta w warstwie platformowej.

3. **Modularność**
   - framework nie powinien być jedną wielką biblioteką,
   - poszczególne obszary powinny być osobnymi repozytoriami Git,
   - projekt końcowy powinien móc korzystać tylko z tych modułów, których faktycznie potrzebuje.

4. **Możliwość testowania na PC**
   - logika i sterowniki powinny być testowalne bez fizycznego mikrokontrolera,
   - powinno być możliwe tworzenie mocków interfejsów sprzętowych.

5. **Kontrolowane zasady dla embedded**
   - framework powinien definiować wspólny zestaw reguł dotyczących stylu i ograniczeń kodu,
   - część reguł może być wspólna,
   - część może zależeć od rodzaju targetu,
   - naruszenia reguł powinny być wykrywane automatycznie.

6. **Rozwój przyrostowy**
   - nie implementujemy obsługi wszystkich możliwych MCU i peryferiów na początku,
   - nowe rodziny, urządzenia i specjalizacje dodajemy dopiero wtedy, gdy są faktycznie potrzebne.

---

# 2. Ogólna architektura

BRICK powinien być rodziną niezależnych bibliotek, a nie pojedynczym monolitycznym repozytorium.

Proponowana struktura repozytoriów:

```text
brick-framework

brick-core
brick-hal
brick-drivers
brick-components
brick-protocols
brick-rules
brick-mocks

brick-platform-stm32
brick-platform-gd32
brick-platform-esp32
brick-platform-pc
```

Każdy moduł powinien posiadać własne repozytorium GitHub.

`brick-framework` jest repozytorium agregującym i może zawierać pozostałe biblioteki jako Git submodules.

Przykład:

```text
brick-framework/
├── libs/
│   ├── core/              -> brick-core
│   ├── hal/               -> brick-hal
│   ├── drivers/           -> brick-drivers
│   ├── components/        -> brick-components
│   ├── protocols/         -> brick-protocols
│   ├── rules/             -> brick-rules
│   └── mocks/             -> brick-mocks
│
├── platforms/
│   ├── stm32/             -> brick-platform-stm32
│   ├── gd32/              -> brick-platform-gd32
│   ├── esp32/             -> brick-platform-esp32
│   └── pc/                -> brick-platform-pc
│
├── cmake/
├── examples/
├── tests/
├── CMakeLists.txt
└── README.md
```

`brick-framework` powinien zawierać możliwie mało właściwego kodu.

Jego zadaniem jest:

- agregowanie modułów,
- dostarczanie wspólnej konfiguracji CMake,
- przechowywanie przykładów integracyjnych,
- uruchamianie testów całego frameworka,
- dokumentowanie sposobu użycia,
- definiowanie kompatybilnych wersji submodułów.

---

# 3. Warstwy projektu

Docelowy przepływ zależności:

```text
Application
    |
    v
Components / Protocols / Drivers
    |
    v
BRICK HAL interfaces
    |
    v
Platform implementation
    |
    v
STM32 / GD32 / ESP32 / PC
```

Wyższe warstwy nie powinny zależeć bezpośrednio od SDK producenta.

Przykład niedozwolonego kierunku:

```cpp
HAL_GPIO_WritePin(...);
```

wewnątrz biblioteki `brick-components` lub `brick-drivers`.

Zamiast tego komponent powinien korzystać z interfejsu:

```cpp
brick::hal::IGpio
```

a odpowiednia platforma dostarczać jego implementację.

---

# 4. brick-core

`brick-core` zawiera kod całkowicie lub prawie całkowicie niezależny od sprzętu.

Przykładowe elementy:

- Timer,
- Timeout,
- RingBuffer,
- FixedQueue,
- MovingAverage,
- MedianFilter,
- Debouncer,
- StateMachine,
- EventQueue,
- Scheduler,
- PID,
- CRC,
- utilities,
- typy pomocnicze,
- algorytmy,
- lekkie kontenery o stałym rozmiarze.

Kod z `brick-core` powinien być możliwy do kompilacji i testowania na PC.

`brick-core` nie powinien zależeć od:

- STM32 HAL,
- GD32 SDK,
- ESP-IDF,
- WinAPI,
- Linux API.

Jeśli wymagana jest funkcja zależna od platformy, powinna być dostarczona przez interfejs z `brick-hal`.

---

# 5. brick-hal

`brick-hal` definiuje wspólne interfejsy sprzętowe.

Przykładowe interfejsy:

```text
IGpio
IClock
IUart
ISpi
II2c
IAdc
IPwm
IFlash
IWatchdog
IRandom
```

HAL ma być możliwie mały.

Nie należy próbować tworzyć kompletnej abstrakcji wszystkich funkcji STM32 HAL, ESP-IDF czy SDK GD32.

Interfejs powinien powstawać wtedy, gdy istnieje realna potrzeba jego użycia.

Przykład:

```cpp
namespace brick::hal
{

class IGpio
{
public:
    virtual ~IGpio() = default;

    virtual void set() = 0;
    virtual void reset() = 0;
    virtual void toggle() = 0;
    virtual bool read() const = 0;
};

}
```

Przykład zegara:

```cpp
namespace brick::hal
{

class IClock
{
public:
    virtual ~IClock() = default;

    virtual uint32_t millis() const = 0;
};

}
```

Wyższe warstwy powinny zależeć od tych interfejsów, a nie od konkretnej implementacji sprzętowej.

---

# 6. brick-platform-*

Repozytoria platformowe zawierają implementacje interfejsów HAL dla konkretnych rodzin sprzętu.

Przykłady:

```text
brick-platform-stm32
brick-platform-gd32
brick-platform-esp32
brick-platform-pc
```

Nie należy tworzyć osobnego repozytorium dla każdej rodziny mikrokontrolera.

Zamiast:

```text
brick-platform-stm32f4
brick-platform-stm32g0
brick-platform-stm32h7
```

preferowane jest:

```text
brick-platform-stm32/
├── common/
├── families/
│   ├── f4/
│   ├── g0/
│   └── h7/
└── ...
```

Rodziny powinny być dodawane tylko wtedy, gdy są używane.

---

# 7. Platforma, rodzina i target

Należy rozróżniać trzy poziomy:

```text
platform/vendor
family
device/target
```

Przykład:

```text
Platform: STM32
Family:   STM32H7
Device:   STM32H743
```

Analogicznie:

```text
Platform: ESP32
Family:   ESP32-S3
Device:   konkretny wariant SoC
```

Nie należy tworzyć specjalizacji dla każdego MCU, jeśli nie jest to wymagane.

Jeżeli dana implementacja GPIO działa identycznie dla F4, G0 i H7, powinna istnieć jedna wspólna implementacja.

Specjalizacja dla rodziny lub konkretnego układu powinna powstawać dopiero wtedy, gdy występuje rzeczywista różnica implementacyjna.

Zasada:

> Struktura kodu powinna odwzorowywać rzeczywiste różnice techniczne, a nie katalog produktów producenta.

---

# 8. Backend producenta

Platforma i biblioteka producenta to niekoniecznie to samo.

Przykładowo dla STM32 w przyszłości mogą istnieć różne backendy:

```text
STM32
├── STM32 HAL
└── STM32 LL
```

Dlatego repo `brick-platform-stm32` powinno umożliwiać w przyszłości wydzielenie backendów bez konieczności przebudowy całego frameworka.

Nie należy jednak implementować wielu backendów z wyprzedzeniem.

Jeśli obecne projekty korzystają ze STM32 HAL, należy rozpocząć wyłącznie od tej implementacji.

---

# 9. brick-platform-pc

PC powinno początkowo być traktowane jako jedna platforma:

```text
brick-platform-pc
```

Przykładowa struktura:

```text
brick-platform-pc/
├── common/
├── windows/
└── linux/
```

Nie należy tworzyć osobnych repozytoriów:

```text
brick-platform-windows
brick-platform-linux
```

dopóki rzeczywiste różnice nie uzasadnią takiego podziału.

Platforma PC może służyć do:

- testów jednostkowych,
- symulacji,
- uruchamiania logiki sterowników bez MCU,
- emulowania urządzeń,
- tworzenia narzędzi developerskich.

---

# 10. brick-drivers

`brick-drivers` zawiera sterowniki konkretnych układów i urządzeń.

Przykłady:

```text
SHT40
BMP280
SSD1306
ST7789
ILI9341
W25Q64
MCP23017
GT911
```

Sterownik urządzenia nie powinien zależeć bezpośrednio od platformy.

Przykład:

```cpp
class Sht40
{
public:
    explicit Sht40(brick::hal::II2c& i2c);

    bool read(float& temperature, float& humidity);

private:
    brick::hal::II2c& i2c_;
};
```

Ten sam driver powinien być możliwy do wykorzystania z:

```text
STM32 I2C
GD32 I2C
ESP32 I2C
MockI2c
PC implementation
```

bez zmian w kodzie sterownika.

---

# 11. brick-components

`brick-components` zawiera elementy logiczne zbudowane na HAL.

Przykłady:

```text
Led
Button
Relay
Buzzer
Encoder
Motor
Fan
Heater
DigitalInput
DigitalOutput
```

Przykład:

```cpp
class Led
{
public:
    explicit Led(brick::hal::IGpio& gpio)
        : gpio_(gpio)
    {
    }

    void on()
    {
        gpio_.set();
    }

    void off()
    {
        gpio_.reset();
    }

private:
    brick::hal::IGpio& gpio_;
};
```

`Led` nie powinien wiedzieć, czy pin pochodzi ze STM32, GD32, ESP32 czy implementacji PC.

---

# 12. brick-protocols

`brick-protocols` powinien zawierać protokoły i formaty komunikacyjne niezależne od platformy.

Przykłady:

```text
Modbus
SLIP
COBS
CRC protocols
proprietary frame protocols
serial framing
binary serialization
```

Jeśli protokół wymaga transportu, powinien korzystać z odpowiedniego interfejsu, np.:

```cpp
IUart
IByteStream
ITransport
```

zamiast odwoływać się bezpośrednio do UART konkretnego MCU.

---

# 13. brick-mocks

`brick-mocks` zawiera implementacje testowe interfejsów BRICK HAL.

Przykłady:

```text
MockGpio
MockClock
MockUart
MockSpi
MockI2c
MockAdc
MockFlash
```

Pozwala to testować kod bez hardware.

Przykład:

```text
Sht40
  |
  v
II2c
  |
  v
MockI2c
```

Możliwe powinno być zasymulowanie np.:

- odebranej ramki,
- zmiany temperatury,
- upływu czasu,
- błędu magistrali,
- timeoutu,
- zmiany wejścia GPIO.

Mocki powinny być proste i deterministyczne.

---

# 14. Capabilities

BRICK powinien posiadać mechanizm opisujący możliwości aktualnego targetu.

Capabilities odpowiadają na pytanie:

> Co ten target sprzętowo lub programowo potrafi?

Przykład:

```cpp
struct PlatformCapabilities
{
    static constexpr bool hasFpu = true;
    static constexpr bool hasDCache = false;
    static constexpr bool hasICache = false;

    static constexpr bool hasHardwareCrc = true;
    static constexpr bool hasHardwareRng = false;

    static constexpr bool hasUsb = true;
    static constexpr bool hasCanFd = false;
};
```

Capabilities nie są regułami coding standard.

Przykład:

```text
hasFpu = true
```

oznacza właściwość platformy.

Natomiast:

```text
allowStdVector = false
```

nie jest capability sprzętu, tylko decyzją projektową i powinno należeć do policies/rules.

---

# 15. Rules / Policies

BRICK powinien definiować zestaw zasad programistycznych.

Rules odpowiadają na pytanie:

> Jak chcemy pisać kod w danym rodzaju projektu?

Przykład:

```cpp
struct EmbeddedPolicy
{
    static constexpr bool allowDynamicAllocation = false;
    static constexpr bool allowExceptions = false;
    static constexpr bool allowRtti = false;

    static constexpr bool allowStdVector = false;
    static constexpr bool allowStdList = false;
    static constexpr bool allowStdMap = false;

    static constexpr bool allowVirtual = true;
};
```

Same flagi policy nie są jednak wystarczającym mechanizmem egzekwowania zasad.

Powinny służyć przede wszystkim do:

- dokumentowania konfiguracji,
- warunkowej konfiguracji biblioteki,
- dostarczania informacji dla kodu,
- generowania raportów,
- wspierania narzędzi buildowych.

Faktyczna weryfikacja reguł powinna odbywać się przez narzędzia statycznej analizy i konfigurację kompilatora.

---

# 16. Poziomy reguł

Reguły powinny być podzielone na kilka poziomów.

## 16.1 General C++ Rules

Reguły wspólne niezależnie od platformy.

Przykłady:

- jednoargumentowe konstruktory oznaczać jako `explicit`,
- używać `override`,
- preferować `nullptr`,
- preferować typy o znanej szerokości tam, gdzie ma to znaczenie,
- unikać niejawnego narrowing,
- kontrolować konwersje signed/unsigned,
- unikać niejasnej własności wskaźników,
- ograniczać globalny stan,
- preferować `constexpr`,
- preferować inicjalizację pól,
- unikać niepotrzebnych makr.

## 16.2 Embedded Rules

Reguły wspólne dla kodu embedded.

Przykładowe założenia początkowe:

- wyjątki wyłączone,
- RTTI wyłączone,
- dynamiczna alokacja pamięci domyślnie zabroniona,
- `malloc/calloc/realloc/free` zabronione,
- `new/delete` domyślnie zabronione,
- `std::vector` domyślnie zabroniony,
- `std::list` domyślnie zabroniony,
- `std::map` domyślnie zabroniony,
- `std::string` ograniczony lub zabroniony w kodzie czasu rzeczywistego,
- preferowany `std::array`,
- preferowany `std::span`,
- preferowane kontenery o stałej pojemności,
- unikać nieograniczonej rekurencji,
- unikać operacji o nieprzewidywalnym koszcie czasowym w ścieżkach realtime,
- jawnie traktować overflow i wraparound tam, gdzie ma to znaczenie.

Nie każda z tych reguł musi pozostać absolutna.

BRICK powinien umożliwiać ich świadome poluzowanie w konkretnym rodzaju projektu.

## 16.3 Platform Rules

Reguły wynikające z konkretnej platformy.

Przykłady STM32:

- wymagania alignment dla DMA,
- cache maintenance przy DMA na układach z DCache,
- ograniczenia dotyczące ISR,
- wymagania dotyczące sekcji pamięci.

Przykłady ESP32:

- ograniczenia dla ISR,
- IRAM requirements,
- zasady FreeRTOS,
- kontrola stack size tasków.

Platform rules powinny powstawać wyłącznie wtedy, gdy wynikają z realnych potrzeb.

---

# 17. brick-rules

`brick-rules` powinno być osobnym repozytorium.

Możliwa struktura:

```text
brick-rules/
├── include/
│   └── brick/
│       └── rules/
│           ├── policy.hpp
│           ├── capabilities.hpp
│           └── config.hpp
│
├── cmake/
│   ├── brick-common.cmake
│   ├── brick-embedded.cmake
│   ├── brick-pc.cmake
│   ├── brick-stm32.cmake
│   ├── brick-gd32.cmake
│   └── brick-esp32.cmake
│
├── clang-tidy/
│   ├── common.yaml
│   ├── embedded.yaml
│   └── pc.yaml
│
├── docs/
│   └── coding-rules.md
│
└── README.md
```

Repozytorium ma stanowić jedno źródło prawdy dla zasad całego ekosystemu.

---

# 18. Egzekwowanie reguł

Reguły nie powinny opierać się wyłącznie na komentarzach i dokumentacji.

Preferowane mechanizmy:

## Compiler warnings

Przykładowo:

```text
-Wall
-Wextra
-Wconversion
-Wsign-conversion
-Wshadow
```

Docelowy zestaw flag powinien być dobierany eksperymentalnie.

Nie należy włączać dużej liczby ostrzeżeń bez analizy ich użyteczności.

## Wyłączenie mechanizmów języka

Dla konfiguracji embedded można rozważyć:

```text
-fno-exceptions
-fno-rtti
```

Jeżeli projekt świadomie nie korzysta z tych mechanizmów.

## clang-tidy

Preferowane narzędzie do statycznej analizy reguł C++.

Powinno wykrywać m.in.:

- ryzykowne konwersje,
- niektóre błędy lifetime,
- nieprawidłowe użycie API,
- niepożądane konstrukcje C++,
- potencjalne problemy wydajnościowe.

Jeżeli standardowe checki nie wystarczą, można w przyszłości dodać własne reguły BRICK.

## Własne skrypty

Proste skrypty mogą wykrywać zabronione API:

```text
std::vector
std::list
malloc
calloc
realloc
free
new
delete
```

Należy jednak unikać prymitywnego wyszukiwania tekstowego tam, gdzie może generować dużo false positive.

---

# 19. Warning zamiast błędu

Podstawowy workflow developerski powinien preferować ostrzeżenia zamiast blokowania kompilacji dla większości reguł jakościowych.

Przykład:

```text
warning: BRICK rule violation: std::vector is discouraged for this target
```

Pozwala to:

- rozwijać kod bez ciągłego blokowania builda,
- stopniowo wdrażać zasady do istniejących projektów,
- świadomie akceptować wyjątki.

Jednocześnie CI może działać bardziej rygorystycznie.

Przykładowy model:

```text
local development:
rule violation -> warning

CI:
selected rule violation -> error/failure
```

Nie wszystkie warningi powinny automatycznie powodować failure CI.

Zestaw reguł krytycznych powinien być utrzymywany oddzielnie.

---

# 20. Wyjątki od zasad

BRICK nie powinien prowadzić do sytuacji, w której zasada istnieje „bo tak”.

Powinna istnieć możliwość jawnego wyjątku.

Przykładowo ESP32 posiada więcej RAM niż wiele typowych MCU i dynamiczna alokacja może być w pewnych częściach aplikacji akceptowalna.

Dlatego policy powinno umożliwiać:

```text
default policy
+
target override
+
project override
```

Przykład:

```text
BRICK Embedded Defaults
    |
    +-- ESP32 adjustments
            |
            +-- Project adjustments
```

Każde odstępstwo od ważnej reguły powinno być świadome i możliwe do odnalezienia w konfiguracji.

---

# 21. Zasada braku nadmiernej abstrakcji

BRICK nie powinien próbować abstrahować wszystkiego.

Abstrakcja ma sens, jeśli:

- co najmniej dwie platformy mogą użyć wspólnego API,
- zwiększa testowalność,
- ogranicza coupling,
- ułatwia przenoszenie kodu,
- nie ukrywa istotnych możliwości platformy.

Jeśli dana funkcja jest bardzo specyficzna dla STM32H7 i nie posiada sensownego odpowiednika na innych platformach, może pozostać elementem:

```text
brick-platform-stm32
```

Nie należy tworzyć sztucznego interfejsu tylko po to, aby wszystko znajdowało się w HAL.

---

# 22. Zasada minimalnego HAL

HAL powinien abstrahować operacje potrzebne aplikacji, a nie całe peryferia producenta.

Przykład:

Sterownik wyświetlacza może potrzebować:

```text
ISpi::write()
IGpio::set()
IGpio::reset()
IClock::delay()
```

Nie musi otrzymywać całego `SPI_HandleTypeDef`.

Pozwala to oddzielić driver od STM32.

Jednocześnie jeśli konkretny driver potrzebuje funkcji bardzo specyficznej dla platformy, należy rozważyć:

- rozszerzenie interfejsu,
- osobny opcjonalny interfejs,
- adapter,
- pozostawienie sterownika jako platform-specific.

---

# 23. Interfejsy runtime vs templates

Domyślnym mechanizmem abstrakcji na początku projektu mogą być klasy interfejsowe i polimorfizm runtime.

Przykład:

```cpp
class IGpio
{
public:
    virtual ~IGpio() = default;
    virtual void set() = 0;
    virtual void reset() = 0;
};
```

Zalety:

- prostota,
- czytelność,
- łatwe mockowanie,
- łatwe dependency injection,
- małe zależności pomiędzy modułami.

BRICK nie powinien na początku template'ować całej warstwy HAL tylko po to, aby uniknąć kosztu funkcji wirtualnych.

Jeżeli profilowanie wykaże rzeczywisty problem, dla krytycznych fragmentów można wprowadzić:

- templates,
- concepts,
- CRTP,
- static polymorphism.

Optymalizacja powinna wynikać z pomiarów.

---

# 24. Dynamiczna alokacja

Domyślną zasadą dla embedded powinno być unikanie dynamicznej alokacji.

Powody:

- fragmentacja heap,
- nieprzewidywalny czas wykonania,
- trudniejsze testowanie,
- trudniejsze określenie maksymalnego zużycia pamięci,
- problemy pojawiające się po długim czasie pracy urządzenia.

Preferowane:

```text
std::array
static buffers
fixed-capacity containers
placement w kontrolowanych przypadkach
stack allocation
compile-time sizing
```

Nie oznacza to absolutnego zakazu heap w każdym projekcie.

ESP32 lub aplikacja Linux/Windows może świadomie posiadać inną policy.

---

# 25. Nazewnictwo

Nazwa całego ekosystemu:

```text
BRICK
```

Repozytoria:

```text
brick-framework
brick-core
brick-hal
brick-drivers
brick-components
brick-protocols
brick-rules
brick-mocks
brick-platform-stm32
brick-platform-gd32
brick-platform-esp32
brick-platform-pc
```

Namespace:

```cpp
namespace brick
{
}
```

Przykładowe podprzestrzenie:

```cpp
brick::core
brick::hal
brick::drivers
brick::components
brick::protocols
brick::platform
brick::rules
```

Include:

```cpp
#include <brick/core/timer.hpp>
#include <brick/hal/gpio.hpp>
#include <brick/drivers/sht40.hpp>
```

CMake targets:

```text
BRICK::Core
BRICK::HAL
BRICK::Drivers
BRICK::Components
BRICK::Protocols
BRICK::Rules
BRICK::Mocks
BRICK::PlatformSTM32
BRICK::PlatformGD32
BRICK::PlatformESP32
BRICK::PlatformPC
```

Nazwy repozytoriów powinny używać lowercase i myślników.

---

# 26. Kierunek zależności między repozytoriami

Preferowane zależności:

```text
brick-core
    |
    +----------------------------------+
                                       |
brick-hal                              |
    |                                  |
    +--> brick-components              |
    +--> brick-drivers                 |
    +--> brick-protocols               |
    |                                  |
    +--> brick-platform-*              |
    |                                  |
    +--> brick-mocks                   |
                                       |
brick-rules ---------------------------+
```

Platformy mogą zależeć od `brick-hal`.

`brick-hal` nie może zależeć od platform.

`brick-drivers` nie powinno zależeć od `brick-platform-*`.

`brick-components` nie powinno zależeć od `brick-platform-*`.

Kod aplikacyjny może zależeć od obu warstw, ponieważ to aplikacja składa konkretne implementacje.

---

# 27. Dependency Injection

Preferowany sposób budowy obiektów:

```cpp
Stm32Gpio ledPin(...);

Led statusLed(ledPin);
```

lub:

```cpp
Stm32I2c i2c(...);

Sht40 sensor(i2c);
```

Aplikacja wybiera implementację.

Biblioteka wyższego poziomu zna tylko interfejs.

Pozwala to na:

```cpp
MockI2c i2c;

Sht40 sensor(i2c);
```

w testach PC.

---

# 28. Konfiguracja targetu

Docelowo framework powinien umożliwiać konfigurację w stylu:

```cmake
set(BRICK_PLATFORM stm32)
set(BRICK_FAMILY stm32h7)
set(BRICK_DEVICE stm32h743)
```

lub równoważnego mechanizmu.

Konfiguracja powinna określać:

- platformę,
- rodzinę,
- target,
- backend,
- policy,
- capabilities.

Nie należy jednak tworzyć skomplikowanego systemu generowania konfiguracji na początku.

Pierwsza wersja może być prostą konfiguracją CMake.

---

# 29. Przykładowa struktura platformy STM32

Przykładowo:

```text
brick-platform-stm32/
├── include/
│   └── brick/
│       └── platform/
│           └── stm32/
│               ├── gpio.hpp
│               ├── uart.hpp
│               ├── spi.hpp
│               ├── i2c.hpp
│               └── capabilities.hpp
│
├── src/
│   ├── common/
│   └── families/
│       ├── f4/
│       ├── g0/
│       └── h7/
│
├── backends/
│   └── hal/
│
├── cmake/
└── CMakeLists.txt
```

Na początku powinny istnieć tylko używane rodziny.

Jeśli pierwszym targetem jest np. STM32F4, tworzymy tylko:

```text
families/f4
```

Pozostałe powstają później.

---

# 30. Przykładowa struktura platformy GD32

```text
brick-platform-gd32/
├── include/
├── src/
│   ├── common/
│   └── families/
├── cmake/
└── CMakeLists.txt
```

Podział na rodziny powinien odpowiadać rzeczywistym różnicom SDK i peryferiów.

Nie należy kopiować struktury STM32 tylko dla zachowania symetrii.

---

# 31. Przykładowa struktura platformy ESP32

```text
brick-platform-esp32/
├── include/
├── src/
│   ├── common/
│   └── families/
│       ├── esp32/
│       ├── esp32-s3/
│       └── esp32-c3/
├── cmake/
└── CMakeLists.txt
```

Ponownie: dodawane są tylko faktycznie używane rodziny.

Integracja powinna docelowo uwzględniać ESP-IDF.

---

# 32. Testy

Każde repozytorium powinno posiadać własne testy, jeśli jego charakter na to pozwala.

Najłatwiej testowalne:

```text
brick-core
brick-drivers
brick-components
brick-protocols
brick-rules
brick-mocks
```

Testy powinny być uruchamiane na PC.

Platform-specific code może posiadać:

- testy kompilacji,
- testy integracyjne,
- testy na hardware,
- Hardware-in-the-Loop w przyszłości.

Pierwszym priorytetem są szybkie testy PC.

---

# 33. CI

Docelowo każdy moduł może posiadać GitHub Actions.

Minimalny pipeline:

```text
configure
build
unit tests
static analysis
```

Dodatkowo framework agregujący powinien sprawdzać kompatybilność aktualnych wersji wszystkich submodułów.

Możliwe konfiguracje:

```text
Linux PC
Windows PC
STM32 compile test
GD32 compile test
ESP32 compile test
```

Nie wszystkie muszą istnieć w pierwszej wersji.

---

# 34. Wersjonowanie

Każda biblioteka powinna być wersjonowana niezależnie.

Preferowane Semantic Versioning:

```text
MAJOR.MINOR.PATCH
```

Przykład:

```text
brick-hal 1.3.0
brick-core 1.5.2
brick-platform-stm32 0.4.0
```

`brick-framework` wskazuje konkretne kompatybilne commity lub tagi poprzez submoduły.

Dzięki temu dany projekt może posiadać reprodukowalny zestaw bibliotek.

---

# 35. Stabilność API

Interfejsy z `brick-hal` są szczególnie ważne, ponieważ wpływają na wiele modułów.

Zmiany HAL powinny być wykonywane ostrożnie.

Preferowane zasady:

- małe interfejsy,
- jedna odpowiedzialność,
- brak API dodawanego „na przyszłość”,
- rozszerzanie przez nowe interfejsy zamiast powiększania jednego ogromnego interfejsu.

Przykład:

zamiast:

```text
IGpio
- set
- reset
- toggle
- read
- setMode
- setPull
- setDriveStrength
- configureInterrupt
- configureAlternateFunction
- ...
```

można oddzielić:

```text
IDigitalInput
IDigitalOutput
IGpioConfigurator
```

jeśli realna potrzeba uzasadnia taki podział.

---

# 36. Kod specyficzny dla platformy

Nie każdy kod musi być przenośny.

Jeżeli funkcja korzysta z unikalnego mechanizmu STM32, może znajdować się w:

```text
brick-platform-stm32
```

Aplikacja może świadomie użyć API platform-specific.

Cel BRICK nie brzmi:

> nigdy nie używaj funkcji specyficznych dla platformy.

Cel brzmi:

> wspólne funkcje powinny mieć wspólne API, a zależności platformowe powinny być jawne i kontrolowane.

---

# 37. Zasada YAGNI

Projekt powinien stosować zasadę:

> You Aren't Gonna Need It.

Nie implementujemy na zapas:

- rodzin MCU, których obecnie nie używamy,
- peryferiów bez realnego zastosowania,
- alternatywnych backendów,
- skomplikowanego generatora konfiguracji,
- własnych kontenerów, jeśli istnieją proste i bezpieczne rozwiązania,
- abstrakcji bez rzeczywistego przypadku użycia.

Każda nowa abstrakcja powinna rozwiązywać konkretny problem.

---

# 38. Początkowy minimalny zakres

Pierwsza wersja BRICK powinna być niewielka.

Proponowane repozytoria na start:

```text
brick-framework
brick-core
brick-hal
brick-rules
brick-mocks
brick-platform-stm32
```

Opcjonalnie platforma używana w aktualnym projekcie:

```text
brick-platform-gd32
```

lub:

```text
brick-platform-esp32
```

Pierwsze interfejsy HAL:

```text
IClock
IDigitalInput
IDigitalOutput
IUart
ISpi
II2c
```

Pierwsze elementy core:

```text
Timer
Timeout
RingBuffer
```

Pierwsze mocki:

```text
MockClock
MockDigitalInput
MockDigitalOutput
MockUart
```

Dopiero później:

```text
drivers
components
protocols
kolejne platformy
kolejne rodziny
```

---

# 39. Przykładowy przypadek użycia

Kod aplikacji:

```cpp
Stm32DigitalOutput relayPin(...);
Stm32Clock clock(...);

brick::components::Relay relay(relayPin);
brick::core::Timer timer(clock);
```

Na PC:

```cpp
MockDigitalOutput relayPin;
MockClock clock;

brick::components::Relay relay(relayPin);
brick::core::Timer timer(clock);
```

`Relay` i `Timer` pozostają bez zmian.

Zmienia się jedynie implementacja zależności.

---

# 40. Definicja sukcesu projektu

BRICK spełnia swoje zadanie, jeśli:

- ten sam driver urządzenia może działać na więcej niż jednej platformie bez modyfikacji,
- ten sam komponent może być testowany na PC,
- zmiana MCU nie wymaga przepisywania logiki aplikacji,
- zależności od SDK producenta są łatwe do znalezienia,
- zasady embedded są automatycznie kontrolowane,
- nowe rodziny MCU można dodawać bez przebudowy architektury,
- projekt końcowy może wybrać tylko potrzebne moduły,
- framework pozostaje wystarczająco prosty, aby nie utrudniał codziennej pracy.

---

# 41. Zasady dla dalszego rozwoju z agentem AI / Codex

Podczas modyfikowania BRICK należy przestrzegać następujących zasad:

1. Nie dodawaj obsługi platformy, rodziny lub urządzenia bez realnej potrzeby.
2. Nie twórz abstrakcji tylko dla zachowania symetrii pomiędzy platformami.
3. Nie dodawaj zależności platform-specific do `brick-core`, `brick-drivers`, `brick-components` ani `brick-protocols`.
4. Przed rozszerzeniem `brick-hal` sprawdź, czy nowa funkcja faktycznie jest wspólną abstrakcją.
5. Preferuj małe interfejsy.
6. Preferuj dependency injection.
7. Kod możliwy do testowania na PC powinien posiadać testy.
8. Nowe reguły embedded powinny zostać opisane w `brick-rules`.
9. Capability sprzętowe i policy projektowe muszą pozostawać oddzielnymi pojęciami.
10. Nie optymalizuj przez templates lub metaprogramowanie bez konkretnego powodu.
11. Nie używaj dynamicznej alokacji w kodzie embedded bez świadomego uzasadnienia.
12. Nie używaj `std::vector`, `std::list`, `std::map` ani podobnych dynamicznych kontenerów w kodzie embedded bez świadomego odstępstwa od policy.
13. Platform-specific code powinien pozostać we właściwym `brick-platform-*`.
14. Wspólna funkcjonalność dwóch platform powinna być przenoszona do odpowiedniej warstwy wspólnej tylko wtedy, gdy daje to realną korzyść.
15. Nie komplikuj CMake przed pojawieniem się rzeczywistej potrzeby.
16. Każda zmiana publicznego API powinna uwzględniać kompatybilność istniejących modułów.
17. Preferuj rozwiązania deterministyczne pod względem pamięci i czasu wykonania.
18. W ścieżkach realtime unikaj operacji o nieprzewidywalnym koszcie.
19. Kod platformowy powinien możliwie cienko opakowywać SDK producenta.
20. Framework ma pomagać tworzyć aplikacje, a nie zmuszać aplikacje do dopasowywania się do frameworka.

---

# 42. Status

Projekt znajduje się na etapie projektowania architektury.

Pierwszym krokiem implementacyjnym powinno być utworzenie:

```text
brick-framework
brick-core
brick-hal
brick-rules
brick-mocks
```

oraz jednej platformy używanej w pierwszym rzeczywistym projekcie.

Architektura powinna być rozwijana iteracyjnie na podstawie praktycznych potrzeb kolejnych projektów.
