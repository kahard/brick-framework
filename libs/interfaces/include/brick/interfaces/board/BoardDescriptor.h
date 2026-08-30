#pragma once

#include <cstdint>

namespace brick::interfaces::board
{

enum class Capability : std::uint32_t
{
    display     = 1u << 0,
    touchscreen = 1u << 1,
    eeprom      = 1u << 2,
    spi_flash   = 1u << 3,
    buzzer      = 1u << 4,
    backlight   = 1u << 5,
    usb_host    = 1u << 6,
    sd_card     = 1u << 7,
};

struct BoardDescriptor
{
    const char*   name         = nullptr;
    const char*   mcu          = nullptr;
    std::uint32_t capabilities = 0;

    constexpr bool has(Capability capability) const { return (capabilities & static_cast<std::uint32_t>(capability)) != 0; }
};

}  // namespace brick::interfaces::board
