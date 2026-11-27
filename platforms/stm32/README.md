# BRICK STM32 platform

Generic STM32 adapters for BRICK, one namespace per family:
`brick::platform::stm32::f1` (headers in `f1/`, sources directly in `src/`)
and `brick::platform::stm32::h7` (headers in `h7/`, sources in `src/h7/`).
Concrete panel wiring and board profiles are supplied by the application or
a separate board package; a build compiles the sources of its family only.

## STM32F1

The drivers deliberately keep the board wiring in a profile.  The display
adapter accepts packed RGB565 BRICK buffers and expands them to the SSD1963
18-bit memory-write format.  It is a synchronous, partial-update device.

## STM32H7

| Header | What |
|---|---|
| `SystemSetup.h` | system clock from the HSE through PLL1 (400 MHz by default), memory protection (external RAM write-through, the empty FMC bank closed), caches |
| `Stm32Sdram.h` | SDRAM on the FMC, kernel clock from PLL2; `begin()` checks a test pattern over the whole memory before the data cache is on |
| `LtdcDisplay.h` | RGB panel on the LTDC, pixel clock from PLL3, one RGB565 layer showing one of one or two frame buffers (`IDisplayDevice` + `IFrameBufferDisplay`); flips at the vertical blanking, panel and backlight enables |
| `Stm32I2cBus.h` | I2C master; the TIMINGR value comes from the board |
| `CapacitiveTouchscreen.h` | FT6x36 or GT911, whichever answers; every finger pressed, moved and released |
| `RttSerialPort.h` | `ISerialPort` over SEGGER RTT (the debug probe), for `SerialLogger` on boards without a free UART |
| `Stm32Pwm.h`, `Stm32PwmBacklight.h`, `Stm32PwmBuzzer.h` | a timer PWM channel; backlight dimming; a sounder without its own oscillator |
| `I2cEeprom.h`, `Stm32SpiBus.h`, `SpiNorFlash.h` | the STM32F1 storage drivers on the H7 HAL |
| `Stm32MemoryProvider.h` | `IMemoryProvider` over fixed regions (on-chip RAM reachable by DMA, external RAM) |
| `Stm32Uart.h` | 8N1 UART up to the kernel clock / 8 (8x oversampling above / 16) on DMA both ways, no interrupts: a receive ring that `read()` empties, a transmit buffer in two halves; `set_baud_rate()` after the queue has drained; framing, noise and overrun counters |
| `Stm32JpegDecoder.h` | baseline JPEG with the hardware codec, YCbCr to RGB565 with the DMA2D into a frame buffer; in bands through a small on-chip buffer or the whole image at once |

The board starts them in this order: `HAL_Init()`, memory protection, system
clock, SDRAM, caches, then the peripherals. Frame buffers are cached
write-through, so the LTDC sees every write without a cache clean.

## Per-project features

USB storage read/write feature macros may be provided by a board package or
project `board_config.h`; when absent they default to `1`.
`BRICK_STM32_USB_FS_MAX_OPEN_FILES` (default `2`) sets how many files
`Stm32UsbFileSystem` can keep open at once; each holds a FatFs `FIL`.

The common `IFile` interface provides `read`, `seek` and `write`; read-only
implementations may keep the default `write` method, which returns zero.
