# Examples

Przykłady aplikacji sprzętowych nie są przechowywane w tym repozytorium, aby
nie powiększać biblioteki Bricka o kompletne projekty PlatformIO i ESPHome.

Aktualne aplikacje testowe i przykłady użycia bibliotek znajdują się w repozytorium:

[github.com/kahard/brick-test-apps](https://github.com/kahard/brick-test-apps)

Przykłady są pogrupowane według platformy i wariantu sprzętowego, między innymi:

- `apps/esp32p4` — panele JC1060 7-calowe,
- `apps/esp32s3` — panele 4-calowe ST7701S,
- `apps/esp32wroom32` — panele CYD,
- `apps/esp8266` — małe wyświetlacze i przyciski.

Repozytorium testowe korzysta z Bricka jako submodułu, dzięki czemu aplikacje
można kompilować przeciwko konkretnej wersji biblioteki.
