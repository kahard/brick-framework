#pragma once

// Project-level ST-280 feature switches. A project may provide a header with
// -include board_config.h and override any of these before including the
// board header. Defaults keep existing applications fully enabled.
#ifndef BRICK_ST280_ENABLE_DISPLAY
#define BRICK_ST280_ENABLE_DISPLAY 1
#endif
#ifndef BRICK_ST280_ENABLE_TOUCH
#define BRICK_ST280_ENABLE_TOUCH 1
#endif
#ifndef BRICK_ST280_ENABLE_EEPROM
#define BRICK_ST280_ENABLE_EEPROM 1
#endif
#ifndef BRICK_ST280_ENABLE_SPI_FLASH
#define BRICK_ST280_ENABLE_SPI_FLASH 1
#endif
#ifndef BRICK_ST280_ENABLE_BUZZER
#define BRICK_ST280_ENABLE_BUZZER 1
#endif
#ifndef BRICK_ST280_ENABLE_BACKLIGHT
#define BRICK_ST280_ENABLE_BACKLIGHT 1
#endif
#ifndef BRICK_ST280_ENABLE_USB_HOST
#define BRICK_ST280_ENABLE_USB_HOST 1
#endif
