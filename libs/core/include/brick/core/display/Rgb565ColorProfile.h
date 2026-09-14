#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace brick::core::display
{

struct Rgb565ColorProfile
{
    static constexpr std::size_t red_entry_count   = 32U;
    static constexpr std::size_t green_entry_count = 64U;
    static constexpr std::size_t blue_entry_count  = 32U;
    static constexpr std::size_t serialized_size   = red_entry_count + green_entry_count + blue_entry_count;

    std::array<std::uint8_t, red_entry_count>   red{};
    std::array<std::uint8_t, green_entry_count> green{};
    std::array<std::uint8_t, blue_entry_count>  blue{};

    static Rgb565ColorProfile identity()
    {
        Rgb565ColorProfile profile;
        for (std::size_t index = 0; index < profile.red.size(); ++index)
            profile.red[index] = static_cast<std::uint8_t>(index);
        for (std::size_t index = 0; index < profile.green.size(); ++index)
            profile.green[index] = static_cast<std::uint8_t>(index);
        for (std::size_t index = 0; index < profile.blue.size(); ++index)
            profile.blue[index] = static_cast<std::uint8_t>(index);
        return profile;
    }

    bool valid() const
    {
        for (const std::uint8_t value : red)
            if (value >= red_entry_count)
                return false;
        for (const std::uint8_t value : green)
            if (value >= green_entry_count)
                return false;
        for (const std::uint8_t value : blue)
            if (value >= blue_entry_count)
                return false;
        return true;
    }

    std::uint16_t apply(std::uint16_t pixel) const
    {
        const std::uint8_t red_index   = static_cast<std::uint8_t>((pixel >> 11U) & 0x1FU);
        const std::uint8_t green_index = static_cast<std::uint8_t>((pixel >> 5U) & 0x3FU);
        const std::uint8_t blue_index  = static_cast<std::uint8_t>(pixel & 0x1FU);
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(red[red_index]) << 11U)
                                          | (static_cast<std::uint16_t>(green[green_index]) << 5U) | blue[blue_index]);
    }
};

}  // namespace brick::core::display
