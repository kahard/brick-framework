# BRICK STM32/F1 platform

Generic STM32/F1 adapters for BRICK. Concrete panel wiring and board
profiles live in separate board repositories (for example, the private
`brick-boards-private` repository contains ST-280).

The drivers deliberately keep the board wiring in a profile.  The display
adapter accepts packed RGB565 BRICK buffers and expands them to the SSD1963
18-bit memory-write format.  It is a synchronous, partial-update device.

## Per-project features

USB storage read/write feature macros may be provided by a board package or
project `board_config.h`; when absent they default to `1`.

The common `IFile` interface provides `read`, `seek` and `write`; read-only
implementations may keep the default `write` method, which returns zero.
