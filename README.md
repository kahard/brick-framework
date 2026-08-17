# BRICK Framework

**BRICK** means **Building Reusable Interfaces, Components and Kits**.

BRICK is a modular C++ framework for portable embedded software. It separates
platform-independent logic from hardware and SDK-specific implementations, so
the same core code can be reused on embedded targets and tested on a PC.

## Project status

The framework is in early development. The first working modules were extracted
from the `wled-ha-panel` project and are currently used on ESP32-S3 hardware.

Current release: **0.1.0** ([`v0.1.0`](https://github.com/kahard/brick-framework/releases/tag/v0.1.0)).

## Repository layout

```text
brick-framework/
├── libs/
│   ├── core/          platform-independent algorithms and components
│   ├── interfaces/    small reusable contracts
│   ├── mocks/         test doubles
│   └── rules/         coding policies and static-analysis rules
├── platforms/
│   ├── esp32/         ESP-IDF and ESP32 adapters
│   ├── pc/            PC implementations and test support
│   └── stm32/         STM32 adapters
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
- an ESP-IDF MIPI-DSI adapter with a JC1060 1024x600 profile.

Core code depends on interfaces, not on ESP-IDF or LVGL. Platform adapters are
kept in `platforms/` and are included only by applications that need them.

Complete hardware examples and smoke-test applications are maintained
separately in [`brick-test-apps`](https://github.com/kahard/brick-test-apps).

## Build and test on PC

Requirements: CMake 3.20 or newer and a C++17 compiler.

```text
cmake -S . -B build -DBRICK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests cover timer scheduling and wraparound, BMP/WAV decoding, and the
audio player using deterministic in-memory fakes.

## Development workflow

- `develop` is the default integration branch.
- Feature and fix branches are created from `develop` and merged through pull
  requests.
- `master` contains stable releases only.
- Releases use Semantic Versioning and immutable tags such as `v0.1.0`.

See [CONTRIBUTING.md](CONTRIBUTING.md) and [VERSIONING.md](VERSIONING.md) for
the complete workflow. The original Polish architecture notes are available
in [docs/architecture-pl.md](docs/architecture-pl.md).

## Design principles

- keep interfaces small and driven by real use cases,
- keep portable code independent from vendor SDKs,
- use dependency injection for platform implementations and mocks,
- prefer deterministic memory and runtime behavior in embedded code,
- add abstractions only when they improve reuse or testability.
