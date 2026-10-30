#pragma once

/*
 * Constant data in program memory (flash).
 *
 * On ESP8266 large constant tables must be placed in flash with PROGMEM and
 * read with pgm_read_* / memcpy_P; on every other target (ESP32, STM32, PC)
 * flash is memory-mapped and these are plain reads. Portable code marks such
 * tables BRICK_PROGMEM and reads them through the helpers below, so the same
 * source builds for ESP8266 and for the PC simulator.
 *
 * This is the only place outside platforms/ allowed to include a toolchain
 * header. It is valid C and C++ because generated asset sources are C.
 */

#if defined(ARDUINO_ARCH_ESP8266) || defined(ESP8266)
#    include <pgmspace.h>
#    define BRICK_PROGMEM PROGMEM
#    define brick_progmem_read_byte(address) pgm_read_byte(address)
#    define brick_progmem_copy(destination, source, bytes) memcpy_P((destination), (source), (bytes))
#else
#    include <string.h>
#    define BRICK_PROGMEM
#    define brick_progmem_read_byte(address) (*(const unsigned char*)(address))
#    define brick_progmem_copy(destination, source, bytes) memcpy((destination), (source), (bytes))
#endif
