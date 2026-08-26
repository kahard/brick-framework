# Changelog

Wszystkie istotne zmiany w BRICK Framework są opisane w tym pliku.

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
