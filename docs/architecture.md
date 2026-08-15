# BRICK — początkowa struktura architektury

## `interfaces` zamiast `hal`

`HAL` pozostaje nazwą pojęcia architektonicznego: warstwa oddzielająca kod
wspólny od implementacji platformy. Moduł zawierający same kontrakty będzie
jednak nazywał się `interfaces`.

Proponowane nazwy:

```text
repozytorium: brick-interfaces
target CMake: BRICK::Interfaces
namespace:    brick::interfaces
```

Implementacje platformowe mogą być nazywane implementacjami HAL, np.
`brick-platform-pc` albo `brick-platform-stm32`.

## Grupy interfejsów

Początkowo grupujemy interfejsy według odpowiedzialności:

```text
interfaces/
├── io/             wejścia i wyjścia cyfrowe oraz analogowe
├── timing/         zegar, opóźnienia, timery i pomiar czasu
├── communication/ UART, SPI, I2C i ogólne transporty
├── storage/        flash, EEPROM i inne trwałe magazyny
├── display/        kontrakty wyświetlania, tylko gdy pojawi się wspólny przypadek użycia
└── system/         watchdog, reset, informacje o systemie i podobne usługi
```

To jest podział organizacyjny, a nie sztywna hierarchia dziedziczenia. Nie
każdy interfejs wyższego poziomu powinien trafiać do `interfaces`. Przykładowo
sterownik konkretnego wyświetlacza powinien pozostać w `drivers`, a interfejs
renderowania powinien powstać dopiero wtedy, gdy będzie używany przez co
najmniej dwa niezależne komponenty lub platformy.

Pierwsze interfejsy powinny być małe i dodawane na podstawie konkretnego
przypadku użycia. Na start wystarczą prawdopodobnie:

```text
io:              IDigitalInput, IDigitalOutput
timing:          IClock
communication:   IUart, ISpi, II2c
```

Nie tworzymy jeszcze interfejsów wyświetlacza, filesystemu ani rozbudowanych
abstrakcji timerów bez pierwszego użytkownika.

## Struktura repozytorium agregującego

```text
brick_framework/
├── cmake/                  wspólna konfiguracja CMake
├── docs/                   architektura i decyzje projektowe
├── examples/               małe przykłady integracyjne
├── libs/
│   ├── core/               kod niezależny od sprzętu
│   ├── interfaces/         kontrakty HAL
│   ├── rules/               policies i reguły budowania
│   └── mocks/               testowe implementacje kontraktów
├── platforms/
│   ├── pc/
│   └── stm32/
├── tests/                  testy integracyjne frameworka
└── README_BRICK.md
```

Repozytorium agregujące nie powinno zawierać implementacji wszystkich modułów.
Docelowo katalogi `libs/*` i `platforms/*` mogą wskazywać na submoduły Git.
