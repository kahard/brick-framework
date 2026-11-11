#pragma once

#include <cstddef>
#include <cstdint>

namespace brick::core::display
{

namespace detail
{
#if defined(__GNUC__)
    // A 32-bit word that may alias the 16-bit pixels or bytes it is written
    // over; without it the compiler could reorder the word accesses with the
    // pixel accesses of the caller (strict aliasing).
    typedef std::uint32_t __attribute__((__may_alias__)) PixelWord;
#else
    typedef std::uint32_t PixelWord;
#endif

    constexpr std::uintptr_t kWordMask = sizeof(PixelWord) - 1U;

    inline bool word_aligned(const void* address)
    {
        return (reinterpret_cast<std::uintptr_t>(address) & kWordMask) == 0U;
    }
}  // namespace detail

/// Sets count RGB565 pixels to color. Two pixels are stored per 32-bit write,
/// which halves the stores into PSRAM and slow internal buses.
inline void fill_rgb565(std::uint16_t* pixels, std::size_t count, std::uint16_t color)
{
    if (count != 0U && !detail::word_aligned(pixels))
    {
        *pixels++ = color;
        --count;
    }
    const std::uint32_t pair  = (static_cast<std::uint32_t>(color) << 16U) | color;
    auto*               words = reinterpret_cast<detail::PixelWord*>(pixels);
    const std::size_t   pairs = count / 2U;
    for (std::size_t index = 0; index < pairs; ++index)
        words[index] = pair;
    if ((count & 1U) != 0U)
        pixels[count - 1U] = color;
}

/// Copies bytes of RGB565 pixels and swaps the two bytes of every pixel: the
/// portable little-endian memory layout becomes the MSB-first order of SPI
/// panels (and back). bytes is even. Source and destination must not
/// overlap. When both are equally aligned, two pixels are converted per
/// 32-bit word instead of byte by byte.
inline void copy_rgb565_swapped(std::uint8_t* destination, const std::uint8_t* source, std::size_t bytes)
{
    const auto  destination_address = reinterpret_cast<std::uintptr_t>(destination);
    const auto  source_address      = reinterpret_cast<std::uintptr_t>(source);
    std::size_t index               = 0;
    if (((destination_address ^ source_address) & detail::kWordMask) == 0U && (source_address & 1U) == 0U)
    {
        // A pixel ahead of the first word boundary.
        if ((source_address & 2U) != 0U && bytes >= 2U)
        {
            destination[0] = source[1];
            destination[1] = source[0];
            index          = 2;
        }
        auto*             out       = reinterpret_cast<detail::PixelWord*>(destination + index);
        const auto*       in        = reinterpret_cast<const detail::PixelWord*>(source + index);
        const std::size_t words     = (bytes - index) / sizeof(detail::PixelWord);
        std::uint32_t     low_bytes = 0x00FF00FFU;
#if defined(__GNUC__)
        // Hides the mask from GCC's byte-permutation pass: on cores without
        // a byte-swap instruction (Xtensa) its rotate sequence takes 7
        // instructions per word, the two masks below 5.
        __asm__("" : "+r"(low_bytes));
#endif
        for (std::size_t word = 0; word < words; ++word)
        {
            const std::uint32_t value = in[word];
            out[word]                 = ((value & low_bytes) << 8U) | ((value >> 8U) & low_bytes);
        }
        index += words * sizeof(detail::PixelWord);
    }
    for (; index + 1U < bytes; index += 2U)
    {
        destination[index]      = source[index + 1U];
        destination[index + 1U] = source[index];
    }
}

}  // namespace brick::core::display
