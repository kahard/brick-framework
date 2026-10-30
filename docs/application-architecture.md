# Application architecture and PC simulation

This document defines how BRICK applications are split into layers so that
the same application code runs on a microcontroller and, unchanged, in a PC
simulator on Windows or Linux. It also tracks the migration of the existing
test applications in `brick-test-apps`.

The coding rules that apply to code running on a target are in
[embedded-coding-rules.md](embedded-coding-rules.md).

## Layers

```text
 ┌────────────────────────────────────────────────────────────────────┐
 │ application        apps/<...>/src/app        portable, no SDK      │
 ├────────────────────────────────────────────────────────────────────┤
 │ core               libs/core, libs/lvgl      portable algorithms   │
 ├────────────────────────────────────────────────────────────────────┤
 │ interfaces         libs/interfaces           contracts only        │
 ├──────────────────────────────┬─────────────────────────────────────┤
 │ board                        │ board                               │
 │ boards/<mcu>/<Board>.h       │ boards/pc/SimulatorBoard.h          │
 ├──────────────────────────────┼─────────────────────────────────────┤
 │ platform                     │ platform                            │
 │ platforms/esp32|esp8266|stm32│ platforms/pc (+ optional SDL2)      │
 ├──────────────────────────────┼─────────────────────────────────────┤
 │ SDK: ESP-IDF, Arduino, HAL   │ OS: Windows, Linux                  │
 └──────────────────────────────┴─────────────────────────────────────┘
```

| Layer | May include | Must not include |
|---|---|---|
| application (`src/app`) | `brick/interfaces/*`, `brick/core/*`, `brick/lvgl/*`, `lvgl.h`, the C++ standard headers allowed by the coding rules | `brick/platform/*`, `brick/boards/*`, any SDK or OS header |
| core, lvgl | `brick/interfaces/*`, standard headers | platforms, boards, SDKs |
| interfaces | standard headers only | everything else |
| board | interfaces, its platform | other platforms |
| platform | interfaces, core, its SDK | boards, applications |
| target entry (`src/target`, `pc/`) | everything | - |

The rule checker in [`libs/rules`](../libs/rules/README.md) enforces the
include column automatically.

## What an application looks like

An application is a set of portable classes plus one thin entry point per
target. It never knows which board it runs on: it receives references to
interfaces.

```text
apps/esp32wroom32/cyd/common/display_touch_smoke/
├── platformio.ini            embedded build
├── Makefile
├── src/
│   ├── app/                  portable application layer
│   │   ├── Services.h        what the application needs from a board
│   │   ├── Application.h/.cpp
│   │   └── TouchTest.h/.cpp  ... the actual test logic
│   └── target/
│       └── main.cpp          builds the board, fills Services, runs the loop
└── pc/
    ├── CMakeLists.txt        PC simulator build
    └── main.cpp              builds the SimulatorBoard, same Services
```

`Services.h` is the contract between the application and a target:

```cpp
struct Services
{
    brick::interfaces::display::IDisplayDevice& display;
    brick::interfaces::display::ITouchscreen&   touch;
    brick::interfaces::time::ITimeProvider&     time;
    brick::interfaces::logging::ILogger&        logger;
};
```

Each application lists only what it uses. The application class takes
`Services` in its constructor and exposes two calls:

```cpp
class Application final
{
public:
    explicit Application(const Services& services);
    bool initialize();   // once, after the board has started
    bool update();       // one iteration; false requests a stop (PC only)
};
```

The loop belongs to the target entry point: `app_main()` / `loop()` on a
microcontroller, `brick::platform::pc::run()` in the simulator. This keeps
timing policy (delays, watchdog feeding, frame pacing) out of the
application.

Board-specific *values* the application needs (screen size, asset partition
offsets, colour profiles) are passed in `Services` or as a small config
struct filled by the target; they are never read from a board header inside
`src/app`.

## The PC simulator

`platforms/pc` implements the interfaces on a desktop OS:

| Interface | PC implementation |
|---|---|
| `IDisplayDevice`, `IFrameBufferDisplay` | `PcFrameBufferDisplay` - RGB565 frame buffers in memory |
| `ITouchscreen` | `PcTouchscreen` - fed by the window (mouse) or by a script |
| `IButton` | `PcButton` - keyboard key in the window, or scripted |
| `IBacklight` | `PcBacklight` - dims the window image |
| `ITimeProvider`, `IClock` | `PcTime` - `std::chrono::steady_clock` |
| `ILogger` | `PcLogger` - stdout |
| `IFileSystem` | `PcFileSystem` - a directory on disk (e.g. a copy of the SD card) |
| memory for pixel buffers | `PcMemory` - aligned heap blocks |

`boards/pc/SimulatorBoard.h` assembles them like a real board, with the
screen size and feature set given as template parameters, so a CYD
application gets a 320x240 simulated screen and a JC8012 application an
800x1280 one.

The window uses SDL2 when CMake finds it; without SDL2 (or with
`--headless`) the simulator runs without a window, which is how CI runs
application smoke tests. Command-line options of every PC entry point:

```text
--headless              no window
--frames N              stop after N application updates
--screenshot FILE.ppm   save the last frame
--touch X,Y@MS          inject a touch at time MS (repeatable)
--fs DIR                root directory for IFileSystem
```

Mocks and fakes for unit tests (counting displays, scripted touch screens,
in-memory file systems) live in `libs/mocks` as `BRICK::Mocks`; they are
separate from the simulator because they record calls instead of emulating
hardware.

## Hardware-specific concerns and how they stay out of `src/app`

The audit of `brick-test-apps` (31 applications, October 2026) found that 11
applications already keep hardware out of their logic, and that the rest leak
it through a small number of recurring patterns. Each gets a portable
replacement:

| Leak found in apps | Portable replacement |
|---|---|
| `heap_caps_malloc` for pixel/DMA/PSRAM buffers (`PixelMemory`, `StripeBuffer`, `FrameBuffers`, ...) | `brick::interfaces::memory::IMemoryProvider` with a memory kind (`internal`, `dma`, `external`); ESP32 implementation on `heap_caps`, PC on aligned heap. Allocation only during `initialize()`. |
| `PartitionAssetSource`, `SdmmcFileSystem`, `SdSpiFileSystem` constructed inside the app | The app receives `IAssetSource&` / `IFileSystem&` in `Services`; the target decides where assets live. |
| `LvglDisplayAdapter`, `LvglTouchAdapter`, `LvglImagePresenter` in `platforms/esp32` | Moved to `libs/lvgl` (`BRICK::Lvgl`, namespace `brick::lvgl`); they only depend on LVGL and interfaces. |
| Board headers included by test classes (panel 480x480) | Test classes take interfaces; only the target knows the board type. |
| UART access in the colour calibration protocol | `brick::interfaces::communication::ISerialPort`. |

## Migration plan

Each stage ends with every application still building for its target.

1. **Environment** - pinned tools, cross-platform scripts, CI building all
   applications (done).
2. **Documentation and rules** - this document, the coding rules and the rule
   checker in report-only mode.
3. **PC platform** - `platforms/pc`, `boards/pc/SimulatorBoard.h`,
   `libs/mocks`, unit tests; CMake target `BRICK::PlatformPC`.
4. **Portable building blocks** - `IMemoryProvider`, `ISerialPort`,
   `libs/lvgl`, asset/file-system injection.
5. **Reference application** - `cyd/common/display_touch_smoke` split into
   `src/app` + `src/target` + `pc/`, running in the simulator and on the CYD.
6. **Roll-out** - the remaining applications, family by family; each one
   gets a PC variant and a headless smoke test in CI.
7. **Enforcement** - the rule checker fails CI for `src/app`, `boards/` and
   `platforms/` (except `platforms/pc`).

### Known debt in the interfaces

`IFileSystem::list_files()` returns `std::vector<std::string>` and
`IFileSystem::open()` returns `std::unique_ptr<IFile>`; both allocate. They
are allowed for now because every caller uses them outside time-critical
paths, but they violate the allocation rules and will be replaced by a
caller-provided buffer (`list_files(path, FileEntry* entries, capacity)`) and
by a caller-owned `IFile` object (`open(path, mode, FileHandle&)`).
