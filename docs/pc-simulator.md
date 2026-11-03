# PC simulator

The PC simulator runs a BRICK application on Windows or Linux with the
application code unchanged: the same `src/app` that goes to the
microcontroller is linked against `platforms/pc` instead of an SDK. The
screen is shown in a window, in a web browser or not at all, which is how
CI runs the smoke test of every application.

This guide explains how the simulator works, how to write an application so
that it runs in it, and how to add the simulator to an application. The layer
rules it builds on are in
[application-architecture.md](application-architecture.md); the coding rules
for the embedded side in [embedded-coding-rules.md](embedded-coding-rules.md).

## What it is

| Mode | How | For |
|---|---|---|
| Window | SDL2, when CMake finds it | everyday development: mouse = touch, space = button, Esc quits |
| Web screen | `--web [PORT]`: a built-in HTTP + WebSocket server | no SDL2 installed, a phone or tablet as the touch screen (`--web-lan`), showing the application to someone else |
| Headless | `--headless` | CI and scripted smoke tests: `--frames`, `--manual-time`, `--touch`, `--press`, `--screenshot` |

Two pieces make it:

- `platforms/pc` implements the interfaces on a desktop OS and is linked as
  `BRICK::PlatformPC`,
- `boards/include/brick/boards/pc/SimulatorBoard.h` assembles them like a
  hardware board, from a `SimulatorBoardConfig` profile of the real board.

### Board profiles

| Profile | Screen | Frame buffers | Button |
|---|---|---|---|
| `SimulatorBoardConfig::cyd()` | 320x240 | partial updates | no |
| `SimulatorBoardConfig::panel480()` | 480x480 | 2 | no |
| `SimulatorBoardConfig::jc1060()` | 1024x600 | 2 | no |
| `SimulatorBoardConfig::jc8012()` | 800x1280 | 2 | no |
| `SimulatorBoardConfig::esp12f_st7789()` | 240x240 | partial updates | yes |

- `frame_buffers = 0` is a partial-update panel: the driver writes
  rectangles. `N` is a scan-out panel with `N` full buffers, as on the
  ESP32-S3 and ESP32-P4 boards, for applications that draw through
  `IFrameBufferDisplay`.
- `button` makes the simulator drive `board.button()`: the web page shows a
  button, the window maps the space key and `--press` works. A profile
  without a button ignores all three, like the hardware.
- `debug_log` lets the PC logger print debug lines.

A new board gets a profile next to the existing ones; the name is the window
title and the page title.

### Interface implementations

| Interface | PC implementation |
|---|---|
| `IDisplayDevice`, `IFrameBufferDisplay` | `PcFrameBufferDisplay`: RGB565 frame buffers in memory |
| `ITouchscreen` | `PcTouchscreen`: fed by the window or the browser (mouse, finger) or by a script |
| `IButton` | `PcButton`: the page button, the space key or a script |
| `IBacklight` | `PcBacklight`: dims the window or the browser image |
| `ITimeProvider`, `IClock` | `PcTime`: `std::chrono::steady_clock`, or manual time |
| `ILogger` | `PcLogger`: stdout |
| `IFileSystem` (SD card) | `PcFileSystem`: a directory on disk |
| `IAssetSource` | `PcFileAssetSource`: the generated asset bundle read from a file |
| `IMemoryProvider` | `PcMemory`: aligned heap blocks |
| `ISerialPort` | `PcSerialPort`: replays the bytes of `--serial-in` |
| `IBuzzer` | `PcBuzzer`: counts beeps |
| `IByteStorage`, `INorFlash` | `PcByteStorage`, `PcNorFlash`: blank memory (the flash answers with a W25Q64 ID) |
| `IWifiStation`, `IWifiAccessPoint` | `PcWifi`: a simulated radio, networks from `--wifi`, scans and joins in simulated time |
| `INetworkInterface` (wired) | `PcHostNetwork`: the PC's own connection, down with `--offline` |
| `IDnsResolver`, `ITcpSocket` | `PcDnsResolver`, `PcTcpSocket`: the real network, failing at once with `--offline` |

Test doubles that record calls instead of emulating hardware are a separate
library, `libs/mocks` (`BRICK::Mocks`), for unit tests.

## How a run works

The PC entry point of an application, `pc/main.cpp`, is the whole glue:

```cpp
#include "../src/app/Application.h"
#include "brick/boards/pc/SimulatorBoard.h"

int main(int argc, char** argv)
{
    brick::platform::pc::SimulatorOptions options;
    if (!brick::platform::pc::SimulatorOptions::parse(argc, argv, options))
        return 2;

    brick::boards::SimulatorBoard board(brick::boards::SimulatorBoardConfig::cyd(), options);
    if (!is_ok(board.begin()))
        return 1;

    my_app::Application application({ board.display(), board.touch(), board.time(), board.logger() });
    return brick::platform::pc::run(application, board.simulator());
}
```

1. `SimulatorOptions::parse()` reads the command line (the table below);
   `--help` and a wrong option print the usage and return false.
2. `SimulatorBoard` builds every peripheral from the profile and the options;
   `begin()` starts them all, even after a failure, and returns the first
   failure.
3. `run()` calls `simulator.begin()`, which opens the window, starts the web
   server, or does neither when headless; then `application.initialize()`;
   then the loop `simulator.step()` / `application.update()` until `step()`
   returns false (window closed, Esc, `--frames` reached) or `update()`
   returns false when it returns `bool`. `simulator.finish()` writes the
   screenshot. The exit code is 0 when everything succeeded, 1 otherwise.

One `step()`, in this order:

1. stop when `--frames` updates are done;
2. with `--manual-time`, advance the clock by the step;
3. apply the scripted touches and presses whose time has come;
4. web screen: publish the rows that changed in the previous update to the
   browsers, apply the input they sent, and with real time pace the loop;
5. window: handle events (mouse, keyboard, close) and repaint;
6. count the update.

`update()` therefore runs between two inputs and two repaints: it must return
quickly, as on the target between two loop iterations.

### Time

`PcTime` is `std::chrono::steady_clock` by default: real time, `delay_ms()`
sleeps. With `--manual-time [MS]` the clock only advances by MS (default 16)
per update, so a run is deterministic: a scripted touch at 300 ms lands on
the same update every time, and a smoke test gives the same screenshot on a
slow CI machine. An application gets this for free when all its timing comes
from `ITimeProvider` (`millis()`, `micros()`, `delay_ms()`); a `std::chrono`
call or a spin loop in `src/app` breaks it, and the coding rules forbid it
anyway.

### Window

SDL2 is optional. CMake links it when it is installed (`mingw-w64-x86_64-SDL2`
in MSYS2, `libsdl2-dev` on Debian and Ubuntu); on Windows `SDL2.dll` is copied
next to the executable. Mouse = touch, space = button (profiles with one),
Esc = quit. `--scale N` zooms the image; without it the simulator chooses
the zoom itself. Without SDL2 an endless run needs `--web` or `--headless`,
and the simulator says so.

### Web screen

`WebScreen` (`platforms/pc/src/WebScreen.cpp`) is a small HTTP + WebSocket
server on its own thread, without external libraries. The page it serves is
`WebScreenPage.h`, compiled into the executable: a canvas that draws the
display, a button for profiles that have one, and a status line.

- `--web [PORT]` starts it, default port 8080, and prints the address.
- It listens on 127.0.0.1 only, so other computers cannot reach it, unless
  `--web-lan` is given: then it binds every interface and a phone on the same
  network opens `http://<this PC>:PORT/`. Windows asks the first time whether
  to allow the executable through the firewall; on a network marked Public
  the answer must be yes, or a rule for the port has to be added by hand.
- Only the rows that changed since the last image are sent, so a text screen
  costs a few kilobytes per change. Nothing is sent while no browser is
  connected; a new browser gets a complete image.
- The page scales the canvas to the browser window, in whole multiples when
  enlarged so that pixels stay sharp, and reconnects by itself when the
  simulator restarts ("disconnected, retrying...").

The protocol, for debugging with the browser's developer tools:

| Direction | Message |
|---|---|
| to the browser (binary, little-endian, several per WebSocket message) | `1 width:u16 height:u16`: screen size, the page clears to black |
| | `2 x:u16 y:u16 w:u16 h:u16 pixels:u16[w*h]`: an RGB565 rectangle |
| | `3 percent:u8`: backlight level, shown as brightness |
| from the browser (text) | `d x y`, `m x y`, `u x y`: touch down, move, up, in display pixels |
| | `b 1`, `b 0`: button pressed, released |

### Scripted runs and smoke tests

Every PC entry point understands:

```text
--headless              no window
--web [PORT]            screen in a web browser instead of a window (default port 8080)
--web-lan               let other devices on the network open the web screen
--frames N              stop after N application updates
--manual-time [MS]      deterministic time, each update advances MS (default 16)
--scale N               window zoom factor
--screenshot FILE.ppm   save the last frame when stopping
--fs DIR                directory used as the SD card (default: sdcard)
--serial-in FILE        bytes the serial port receives
--touch X,Y@MS[+DUR]    touch at MS milliseconds for DUR ms (repeatable)
--press MS[+DUR]        press the button at MS for DUR ms (repeatable)
--offline               no internet: DNS lookups and connections fail
--wifi SSID[:PASS[:RSSI]]  a network the simulated Wi-Fi finds (repeatable)
```

A smoke test is the application run headless, with deterministic time, for a
fixed number of updates, with scripted input and a screenshot at the end:

```text
cyd_display_touch_smoke --headless --manual-time --frames 120 --fs build/sdcard/cyd_display_touch_smoke \
    --touch 160,120@100+200 --touch 40,200@400+100 --screenshot build/screenshots/cyd_display_touch_smoke.ppm
```

It fails when `initialize()` fails or the screenshot cannot be written; what
the application logged tells the rest. The screenshot is a binary PPM that
any image viewer or Pillow opens. In `brick-test-apps`, `brick_app_simulator()`
registers exactly this command as a CTest for every application
(`make pc-test` in the repository root).

## Writing an application that runs in the simulator

The same code runs on the microcontroller and on the PC because the
application sees only interfaces. Everything below follows from that.

### Structure

```text
src/app/      portable: Services.h, Application.h/.cpp, the actual logic
src/target/   embedded entry point: chooses the board, fills Services, runs the loop
pc/           main.cpp (SimulatorBoard, the same Services) and the simulator build description
```

`Services.h` lists what the application needs, as references to interfaces,
nothing else:

```cpp
struct Services
{
    brick::interfaces::display::IDisplayDevice& display;
    brick::interfaces::display::ITouchscreen&   touch;
    brick::interfaces::time::ITimeProvider&     time;
    brick::interfaces::logging::ILogger&        logger;
};
```

`Application` takes `Services` in its constructor and has
`Status initialize()` (once, after the board started) and `void update()`
(one iteration; `bool update()` where `false` should stop the simulator).

### Rules that decide whether it runs on the PC

1. **Includes.** `src/app` includes only `brick/interfaces/*`, `brick/core/*`,
   `brick/lvgl/*`, `lvgl.h` and the standard headers the coding rules allow.
   No `brick/platform/*`, no `brick/boards/*`, no SDK header. The rule
   checker (`libs/rules`, `make rules-check` in brick-test-apps) reports a
   violation before the PC build fails on it.
2. **Services, not boards.** The application never names a board type or a
   driver; the two entry points fill the same `Services` from different
   objects. Board-specific values (screen size, partition offsets, colour
   profiles) travel in `Services` or in a small config struct the entry point
   fills.
3. **`update()` never blocks.** No waiting loops on hardware, no delays longer
   than a frame; keep state and continue in the next update. The simulator
   repaints and reads input between updates, so a blocking `update()` freezes
   the window and the page.
4. **Time from `ITimeProvider`.** Then `--manual-time` works and the smoke
   test is deterministic.
5. **Memory in `initialize()`.** Pixel buffers and other large blocks come
   from `IMemoryProvider` (with the memory kind: internal, DMA, external)
   during `initialize()`; `update()` does not allocate.
6. **RGB565 is little-endian.** A native `std::uint16_t` per pixel, like the
   generated assets. Code that packs bytes by hand writes the low byte first;
   swapped colours in the simulator mean the packing is wrong (this is how a
   big-endian packing bug in the ESP-12F button test was found).
7. **Constant tables in flash.** `BRICK_PROGMEM` plus `brick_progmem_read_byte()`
   / `brick_progmem_copy()` from `brick/core/memory/ProgramMemory.h`; on the
   PC and on ESP32 that is plain memory, on ESP8266 PROGMEM.
8. **Build-time values.** What `platformio.ini` passes with `-D` is passed to
   the simulator as compile definitions; what a Makefile generates (fonts,
   assets) is generated before the simulator builds (`DEFINES` and `PREBUILD`
   below).

### What each entry point hands over

| The application needs | Hardware entry point (ESP32) | PC entry point |
|---|---|---|
| display, touch, backlight | `board.display()`, `board.touch()`, `board.backlight()` | the same from `SimulatorBoard` |
| a push button | `board.button()` (ESP-12F) | `board.button()`, driven only for a profile with `button` |
| the asset bundle | `PartitionAssetSource{ "assets" }` | `PcFileAssetSource(BRICK_PC_ASSET_BUNDLE)`, the generated bundle file |
| the SD card | `board.sd()` (SD SPI or SDMMC file system) | `board.sd()`: the directory of `--fs`, default `sdcard` |
| a serial port | `EspUartPort` | `board.serial()`: replays `--serial-in` |
| Wi-Fi | `EspWifi` | `board.wifi()`: simulated, networks from `--wifi` |
| wired network, internet | `EspEthernet` | `board.ethernet()`: the PC's connection, down with `--offline` |
| DNS, TCP | `EspDnsResolver`, `EspTcpSocket` | `PcDnsResolver`, `PcTcpSocket` |
| memory | `EspMemory` (`heap_caps`) | `board.memory()` |
| time, logging | `FreeRtosTime`, `EspIdfLogger` | `board.time()`, `board.logger()` |
| EEPROM, NOR flash | the I2C EEPROM and SPI flash drivers | `board.eeprom()`, `board.flash()`: blank memory |
| a buzzer | the board buzzer | `board.buzzer()`: counts beeps |

An application with assets, for example (the embedded entry point is the same
with `PartitionAssetSource`):

```cpp
brick::boards::SimulatorBoard          board(brick::boards::SimulatorBoardConfig::panel480(), options);
brick::platform::pc::PcFileAssetSource flash_assets(BRICK_PC_ASSET_BUNDLE);
if (!is_ok(board.begin()) || !is_ok(flash_assets.begin()))
    return 1;

Application application({ board.display(), board.display(), board.touch(), flash_assets, board.sd(),
                          board.memory(), board.time(), board.logger() });
return brick::platform::pc::run(application, board.simulator());
```

`BRICK_PC_ASSET_BUNDLE` is a compile definition with the path of the bundle
the application's Makefile generates.

### Adding the simulator in brick-test-apps

Two files next to `src/`:

- `pc/main.cpp`, as above;
- `pc/simulator.cmake`, one call:

```cmake
brick_app_simulator(cyd_display_touch_smoke
  SMOKE_ARGS --touch 160,120@100+200 --touch 40,200@400+100)
```

`brick_app_simulator(<name> [LVGL] [FRAMES n] [DEFINES name=value...]
[PREBUILD make-targets...] [SMOKE_ARGS args...] [SOURCES files...])`:

- the name must be unique across the repository: all simulators build in one
  CMake project; the convention is `<board>_<application>`,
- every `.cpp` and `.c` under `src/app` plus `pc/main.cpp` is compiled, so a
  new source file needs no entry here (the ESP-IDF component in
  `src/CMakeLists.txt` lists files explicitly: add it there),
- `LVGL` links the LVGL version pinned in `platformio-versions.ini` (fetched
  by CMake, so the first configuration needs the network) and the BRICK LVGL
  adapters,
- `FRAMES` is the length of the smoke test (default 120 updates), `SMOKE_ARGS`
  its scripted input,
- `DEFINES` are the values `platformio.ini` passes with `-D`,
- `PREBUILD` names targets of the application's Makefile that generate
  sources or assets; they run at configuration time and before every build,
- `SOURCES` adds files outside `src/app`, for example shared code in `apps/shared`.

`pc/CMakeLists.txt` in the repository finds every `pc/simulator.cmake`
automatically. In the application's directory `make sim` opens the window,
`make sim-web` the browser page and `make sim-lan` the page for the whole
network (`WEB_PORT=...`, `SIM_ARGS="..."` for the other options); from the
root `make pc` builds all simulators and `make pc-test` runs the smoke tests.

### Adding the simulator in another project

With BRICK as a submodule in `libs/brick`, a CMake project of a few lines:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_app_pc LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/brick brick)  # BRICK::PlatformPC and brick_add_simulator()

file(GLOB_RECURSE app_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/app/*.cpp")
brick_add_simulator(my_app ${app_sources} pc/main.cpp)
target_include_directories(my_app PRIVATE src/app)
target_compile_definitions(my_app PRIVATE MY_OPTION=1)  # what platformio.ini passes with -D
```

`brick_add_simulator()` links `BRICK::PlatformPC`, links the MinGW runtime
statically on Windows and copies `SDL2.dll` next to the executable. Needed on
the host: CMake 3.20+, a C++17 compiler (MinGW-w64 from MSYS2 on Windows,
GCC or Clang on Linux), optionally SDL2. Then:

```text
cmake -S . -B build -G Ninja
cmake --build build
build/my_app --web
```

`BRICK_BUILD_TESTS` stays off when BRICK is a subdirectory; `BRICK_PC_SDL2=OFF`
builds without the window on purpose.

### Checklist for a new application

- `src/app` has no platform, board or SDK include (`make rules-check`).
- `Services` lists only interfaces; both entry points fill the same list.
- `initialize()` allocates and may fail with a `Status`; `update()` does not
  allocate and never blocks.
- All timing comes from `ITimeProvider`; the application behaves with
  `--manual-time`.
- Every `-D` of `platformio.ini` has its `DEFINES` twin; generated sources are
  in `PREBUILD`.
- The profile matches the real screen; `button` only when the board has one.
- The smoke test (`FRAMES`, `SMOKE_ARGS`) exercises the main path and the
  screenshot shows the expected screen.
- `make sim-web` opens the page; `make sim-lan` is reachable from a phone.

## Troubleshooting

- **"Cannot open the web screen on port 8080"**: the port is taken, usually by
  a simulator still running. Use another port (`make sim-web WEB_PORT=8081`)
  or stop the other one.
- **Other devices cannot connect**: the simulator was started without
  `--web-lan` (`netstat` shows `127.0.0.1:8080` instead of `0.0.0.0:8080`),
  or the firewall blocks it. Windows marks office Wi-Fi as Public and blocks
  inbound connections there by default.
- **"Without SDL2 an endless run needs --web or --headless"**: install SDL2
  or use `make sim-web`.
- **Swapped colours in the simulator, right on the panel**: RGB565 byte
  order, rule 6 above.
- **The page stays black**: nothing was drawn yet. The browser gets the first
  image only after the application draws something; check that
  `initialize()` succeeded in the log.
- **A scripted touch is missed**: times are simulated milliseconds. With real
  time the update at 300 ms may not exist on a slow machine; use
  `--manual-time`, as the smoke tests do.
- **The smoke test passes locally and fails in CI**: look for real time, real
  network or files outside `--fs` in the application; CI has none of them.
