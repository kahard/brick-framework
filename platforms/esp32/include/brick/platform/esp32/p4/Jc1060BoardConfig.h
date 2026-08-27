#pragma once

// Project-level JC1060 7-inch feature switches. A project may override these
// from board_config.h (for example with -include board_config.h).
#ifndef BRICK_JC1060_ENABLE_DISPLAY
#define BRICK_JC1060_ENABLE_DISPLAY 1
#endif
#ifndef BRICK_JC1060_ENABLE_TOUCH
#define BRICK_JC1060_ENABLE_TOUCH 1
#endif
#ifndef BRICK_JC1060_ENABLE_BACKLIGHT
#define BRICK_JC1060_ENABLE_BACKLIGHT 1
#endif
#ifndef BRICK_JC1060_ENABLE_SDMMC
#define BRICK_JC1060_ENABLE_SDMMC 1
#endif
