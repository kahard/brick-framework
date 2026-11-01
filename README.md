# BRICK Framework

**BRICK** means **Building Reusable Interfaces, Components and Kits**.

BRICK is a modular C++ framework for portable embedded software. It separates
platform-independent logic from hardware and SDK-specific implementations, so
the same core code can be reused on embedded targets and tested on a PC.

## Project status

The framework is in early development. The first working modules were extracted
from the `wled-ha-panel` project and are currently used on ESP32-S3 hardware.

Current release: **0.2.0** ([`v0.2.0`](https://github.com/kahard/brick-framework/releases/tag/v0.2.0)).

## Repository layout

```text
brick-framework/
├── libs/
│   ├── core/          platform-independent algorithms and components
│   ├── interfaces/    small reusable contracts
│   ├── lvgl/          LVGL v9 adapters for the display and touch contracts
│   ├── mocks/         test doubles (BRICK::Mocks)
│   └── rules/         coding policies and static-analysis rules
├── platforms/
│   ├── esp32/         ESP-IDF and ESP32 adapters
│   ├── esp8266/       Arduino ESP8266 adapters
│   ├── pc/            PC platform and simulator (BRICK::PlatformPC)
│   └── stm32/         STM32 adapters
├── boards/            ready-made board packages (wiring profiles + IBoard)
├── tests/             PC regression tests
├── examples/          links to hardware examples in brick-test-apps
└── cmake/             shared build configuration
```

## Current modules

The initial implementation includes:

- timing interfaces and a wraparound-safe periodic timer,
- storage interfaces and an ESP32 file-system adapter,
- BMP decoding to native LVGL RGB565,
- WAV decoding and audio buffering,
- audio-player interfaces and a periodic player,
- LVGL image presentation,
- ESP32 PWM audio output,
- portable display, touchscreen, and backlight interfaces,
- touch calibration and coordinate mapping,
- an ESP-IDF MIPI-DSI adapter,
- networking: Wi-Fi station and hotspot, Ethernet, DNS and TCP behind
  interfaces, with a portable Wi-Fi manager, a link selector (Ethernet before
  Wi-Fi) and an HTTP connectivity probe, on ESP32, ESP32-P4 (ESP32-C6
  co-processor), ESP8266 and the PC simulator (see
  [docs/network.md](docs/network.md)),
- board packages for JC1060, JC8012, ESP32-S3 480x480 panel, CYD and
  ESP-12F ST7789 boards.

Core code depends on interfaces, not on ESP-IDF or LVGL. Platform adapters are
kept in `platforms/` and are included only by applications that need them.

Board packages in `boards/include/brick/boards/` combine platform adapters with
concrete pin and panel profiles. Each board is a `<Name>BoardTemplate<Features>`
class with a `<Name>Board` alias for the default feature set; the wiring lives
in a sibling `<name>/Profile.h`. Add `boards/include` to the include path (or
link `BRICK::Boards` in CMake) to use them.

Complete hardware examples and smoke-test applications are maintained
separately in [`brick-test-apps`](https://github.com/kahard/brick-test-apps).

## Build and test on PC

Requirements: CMake 3.20 or newer and a C++17 compiler.

```text
cmake -S . -B build -DBRICK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Formatting uses clang-format 17, pinned in `requirements.txt` together with
Pillow for the asset tools. `make setup` installs both into `.venv`; then
`make format` / `make format-check` work the same on Windows, Linux and macOS.

The tests cover timer scheduling and wraparound, BMP/WAV decoding, and the
audio player using deterministic in-memory fakes.

## PC simulator

`platforms/pc` implements the interfaces on Windows and Linux (display in
memory, touch, button, backlight, time, logger, a directory as SD card) and
`boards/include/brick/boards/pc/SimulatorBoard.h` assembles them like a real
board. With SDL2 installed the display is shown in a window (mouse = touch);
with `--web [PORT]` it is shown in a web browser instead (a built-in server,
default http://localhost:8080/; `--web-lan` lets a phone or tablet on the
network open it too); with `--headless`, applications run without a window,
which is how CI runs their smoke tests. Without SDL2 an endless run needs
`--web` or `--headless`. Link `BRICK::PlatformPC` and use
`brick_add_simulator()` from `cmake/BrickSimulator.cmake`.

Test doubles that record calls (displays, touch screens, file systems,
loggers) are in `libs/mocks` (`BRICK::Mocks`).

## Development workflow

- `develop` is the default integration branch.
- Feature and fix branches are created from `develop` and merged through pull
  requests.
- `master` contains stable releases only.
- Releases use Semantic Versioning and immutable tags such as `v0.2.0`.

See [CONTRIBUTING.md](CONTRIBUTING.md) and [VERSIONING.md](VERSIONING.md) for
the complete workflow. The original Polish architecture notes are available
in [docs/architecture-pl.md](docs/architecture-pl.md).

## Architecture and rules

- [Application architecture and PC simulation](docs/application-architecture.md):
  layers, how applications stay portable, the PC simulator and the migration
  plan.
- [Embedded coding rules](docs/embedded-coding-rules.md): rules for code that
  runs on a microcontroller, checked by [`libs/rules`](libs/rules/README.md).

## Design principles

- keep interfaces small and driven by real use cases,
- keep portable code independent from vendor SDKs,
- use dependency injection for platform implementations and mocks,
- prefer deterministic memory and runtime behavior in embedded code,
- add abstractions only when they improve reuse or testability.
