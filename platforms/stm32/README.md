# BRICK STM32/F1 platform

Arduino STM32 adapters for BRICK.  The initial profile targets the
ST-280 panel reference hardware: STM32F105VC, an SSD1963 on
an 8-bit parallel bus and an FT5x06-compatible capacitive touch controller.

The drivers deliberately keep the board wiring in a profile.  The display
adapter accepts packed RGB565 BRICK buffers and expands them to the SSD1963
18-bit memory-write format.  It is a synchronous, partial-update device.

`profiles::st280_480x272()` is based on the pin mapping and
timings in the reference project.  The `WR` pin is configurable because the
legacy ST280/281 and ST286/595 board families use different pins.

## Per-project features

Include `brick/platform/stm32/f1/St280BoardConfig.h` through the project's
`board_config.h` (normally with PlatformIO's `-include board_config.h`).
Feature macros default to `1`; set a macro to `0` to omit the corresponding
board source from the PlatformIO `BuildSources` list. USB storage has separate
read and write switches. Setting `BRICK_ST280_ENABLE_USB_WRITE` to `0` also
selects FatFs read-only mode.

The common `IFile` interface provides `read`, `seek` and `write`; read-only
implementations may keep the default `write` method, which returns zero.
