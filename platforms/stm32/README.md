# BRICK STM32/F1 platform

Generic STM32/F1 adapters for BRICK. Concrete panel wiring and board
profiles are supplied by the application or a separate board package.

The drivers deliberately keep the board wiring in a profile.  The display
adapter accepts packed RGB565 BRICK buffers and expands them to the SSD1963
18-bit memory-write format.  It is a synchronous, partial-update device.

## Per-project features

USB storage read/write feature macros may be provided by a board package or
project `board_config.h`; when absent they default to `1`.
`BRICK_STM32_USB_FS_MAX_OPEN_FILES` (default `2`) sets how many files
`Stm32UsbFileSystem` can keep open at once; each holds a FatFs `FIL`.

The common `IFile` interface provides `read`, `seek` and `write`; read-only
implementations may keep the default `write` method, which returns zero.
