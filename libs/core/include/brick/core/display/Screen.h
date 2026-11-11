#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/core/display/Rgb565Pixels.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/WritablePixelBuffer.h"

namespace brick::core::display
{

/// Small, device-independent facade for the common display operations used by
/// applications.  Low-level drivers remain available through IDisplayDevice.
class Screen final
{
public:
    /// RGB565 drawing surface for one screen area, backed by pixels the caller
    /// owns (typically one buffer reused for every canvas). It is invalid when
    /// the area is empty or does not fit into the caller's pixels.
    class Canvas final
    {
    public:
        Canvas(Screen& screen, brick::interfaces::display::DisplayRect area, std::uint16_t* pixels,
               std::size_t capacity)
            : screen_(screen), area_(area),
              status_(area.width <= 0 || area.height <= 0 || pixels == nullptr ? Status::invalid_argument :
                      static_cast<std::size_t>(area.width) * static_cast<std::size_t>(area.height) > capacity ?
                                                                                 Status::no_memory :
                                                                                 Status::ok),
              pixels_(is_ok(status_) ? pixels : nullptr)
        {
        }

        bool                                            valid() const { return pixels_ != nullptr; }
        explicit                                        operator bool() const { return valid(); }
        brick::interfaces::display::WritablePixelBuffer buffer()
        {
            return { reinterpret_cast<std::uint8_t*>(pixels_),
                     static_cast<std::uint32_t>(area_.width),
                     static_cast<std::uint32_t>(area_.height),
                     static_cast<std::size_t>(area_.width) * sizeof(std::uint16_t),
                     brick::interfaces::display::PixelFormat::rgb565,
                     false };
        }
        /// clear() and text() return the canvas for chaining; the first failure
        /// is kept and reported by present().
        Canvas& clear(std::uint16_t color)
        {
            auto view = buffer();
            keep_first_failure(screen_.fill(view, color));
            return *this;
        }
        template <typename Glyph>
        Canvas& text(std::int32_t x, std::int32_t y, const char* value, const char* characters, const Glyph* glyphs,
                     std::size_t count, std::uint16_t color)
        {
            auto view = buffer();
            keep_first_failure(screen_.draw_text(view, x, y, value, characters, glyphs, count, color));
            return *this;
        }
        /// Status::no_memory when the area did not fit the caller's pixels,
        /// otherwise the first drawing failure or the display transfer status.
        Status present() const
        {
            if (!is_ok(status_))
                return status_;
            const brick::interfaces::display::PixelBuffer view{
                reinterpret_cast<const std::uint8_t*>(pixels_),
                static_cast<std::uint32_t>(area_.width),
                static_cast<std::uint32_t>(area_.height),
                static_cast<std::size_t>(area_.width) * sizeof(std::uint16_t),
                brick::interfaces::display::PixelFormat::rgb565,
                false
            };
            if (const Status status = screen_.draw(area_, view); !is_ok(status))
                return status;
            return screen_.wait_for_transfer_complete(1000);
        }

    private:
        void keep_first_failure(Status status)
        {
            if (is_ok(status_))
                status_ = status;
        }

        Screen&                                 screen_;
        brick::interfaces::display::DisplayRect area_;
        Status                                  status_;
        std::uint16_t*                          pixels_;
    };

    explicit Screen(brick::interfaces::display::IDisplayDevice& device) : device_(device) {}

    /// Canvas for area drawing into pixels (capacity pixels of RGB565).
    Canvas create_canvas(brick::interfaces::display::DisplayRect area, std::uint16_t* pixels, std::size_t capacity)
    {
        return Canvas(*this, area, pixels, capacity);
    }

    template <std::size_t Capacity>
    Canvas create_canvas(brick::interfaces::display::DisplayRect area, std::array<std::uint16_t, Capacity>& pixels)
    {
        return Canvas(*this, area, pixels.data(), Capacity);
    }

    /// Convenience overload for a full-width strip starting at the top edge.
    template <std::size_t Capacity>
    Canvas create_canvas(std::int32_t height, std::array<std::uint16_t, Capacity>& pixels)
    {
        const auto display_size = device_.size();
        return create_canvas({ 0, 0, static_cast<std::int32_t>(display_size.width), height }, pixels);
    }

    brick::interfaces::display::DisplaySize size() const { return device_.size(); }

    Status draw(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer)
    {
        if (area.empty() || area.width != static_cast<std::int32_t>(buffer.width)
            || area.height != static_cast<std::int32_t>(buffer.height) || !display_rect().contains(area)
            || buffer.format != device_.pixel_format())
        {
            return Status::invalid_argument;
        }
        return device_.draw_buffer(area, buffer);
    }

    Status present(const brick::interfaces::display::PixelBuffer& buffer)
    {
        const auto display_size = device_.size();
        return draw(
            { 0, 0, static_cast<std::int32_t>(display_size.width), static_cast<std::int32_t>(display_size.height) },
            buffer);
    }

    /// Fills an area of the display with color. A device with its own fill
    /// path (IDisplayDevice::fill_rect(), e.g. a DMA fill) does it without the
    /// CPU; otherwise the color is drawn from the caller's pixels, in strips as
    /// tall as they allow. The pixels must hold at least one row of the area
    /// either way, so the call works on every device.
    Status fill_area(brick::interfaces::display::DisplayRect area, std::uint16_t color, std::uint16_t* pixels,
                     std::size_t capacity)
    {
        if (area.empty() || pixels == nullptr || capacity < static_cast<std::size_t>(area.width)
            || !display_rect().contains(area))
            return Status::invalid_argument;
        if (const Status status = device_.fill_rect(area, color); status != Status::not_supported)
            return status;
        const auto strip_rows = static_cast<std::int32_t>(
            std::min(capacity / static_cast<std::size_t>(area.width), static_cast<std::size_t>(area.height)));
        for (std::int32_t row = 0; row < area.height; row += strip_rows)
        {
            const brick::interfaces::display::DisplayRect strip{ area.x, area.y + row, area.width,
                                                                 std::min(strip_rows, area.height - row) };
            if (const Status status = create_canvas(strip, pixels, capacity).clear(color).present(); !is_ok(status))
                return status;
        }
        return Status::ok;
    }

    template <std::size_t Capacity>
    Status fill_area(brick::interfaces::display::DisplayRect area, std::uint16_t color,
                     std::array<std::uint16_t, Capacity>& pixels)
    {
        return fill_area(area, color, pixels.data(), Capacity);
    }

    Status fill(brick::interfaces::display::WritablePixelBuffer& buffer, std::uint16_t color) const
    {
        if (buffer.data == nullptr || buffer.format != brick::interfaces::display::PixelFormat::rgb565)
            return Status::invalid_argument;
        const std::size_t row_bytes = static_cast<std::size_t>(buffer.width) * sizeof(std::uint16_t);
        if (buffer.stride_bytes == row_bytes)
        {
            // Packed rows: one run over the whole buffer.
            fill_rgb565(reinterpret_cast<std::uint16_t*>(buffer.data),
                        static_cast<std::size_t>(buffer.width) * buffer.height, color);
            return Status::ok;
        }
        for (std::uint32_t y = 0; y < buffer.height; ++y)
            fill_rgb565(reinterpret_cast<std::uint16_t*>(buffer.data + y * buffer.stride_bytes), buffer.width, color);
        return Status::ok;
    }

    /// Renders a 1-bit bitmap font into an RGB565 buffer.  The glyph type is
    /// intentionally structural so generated application fonts can be used
    /// without a framework dependency.
    template <typename Glyph>
    Status draw_text(brick::interfaces::display::WritablePixelBuffer& buffer, std::int32_t x, std::int32_t y,
                     const char* text, const char* characters, const Glyph* glyphs, std::size_t count,
                     std::uint16_t color) const
    {
        if (buffer.data == nullptr || text == nullptr || characters == nullptr || glyphs == nullptr
            || buffer.format != brick::interfaces::display::PixelFormat::rgb565)
            return Status::invalid_argument;
        for (const char* cursor = text; *cursor != '\0'; ++cursor)
        {
            std::size_t index = 0;
            while (index < count && characters[index] != *cursor)
                ++index;
            if (index >= count)
                continue;
            const Glyph& glyph = glyphs[index];
            draw_glyph_(buffer, x, static_cast<std::int32_t>(y + glyph.top), glyph, color);
            x += glyph.width + 1;
            if (x >= static_cast<std::int32_t>(buffer.width))
                break;
        }
        return Status::ok;
    }

    Status wait_for_transfer_complete(std::uint32_t timeout_ms)
    {
        return device_.wait_for_transfer_complete(timeout_ms);
    }

private:
    brick::interfaces::display::DisplayRect display_rect() const
    {
        const auto display_size = device_.size();
        return { 0, 0, static_cast<std::int32_t>(display_size.width), static_cast<std::int32_t>(display_size.height) };
    }

    /// Column of the highest set bit of a non-zero bitmap byte (bit 7 is
    /// column 0): one instruction where the compiler has a count of leading
    /// zeros (NSAU on Xtensa, CLZ on ARM).
    static std::int32_t first_set_column_(std::uint32_t byte)
    {
#if defined(__GNUC__)
        return __builtin_clz(byte) - 24;
#else
        std::int32_t column = 0;
        while ((byte & 0x80U) == 0U)
        {
            byte <<= 1U;
            ++column;
        }
        return column;
#endif
    }

    /// Sets the glyph's pixels with its top-left corner at (left, top). The
    /// part inside the buffer is computed once per glyph and applied to each
    /// bitmap byte as a mask, so the loop visits only the set pixels, with no
    /// bounds checks.
    template <typename Glyph>
    static void draw_glyph_(brick::interfaces::display::WritablePixelBuffer& buffer, std::int32_t left,
                            std::int32_t top, const Glyph& glyph, std::uint16_t color)
    {
        const auto width        = static_cast<std::int32_t>(buffer.width);
        const auto height       = static_cast<std::int32_t>(buffer.height);
        const auto first_row    = std::max<std::int32_t>(0, -top);
        const auto end_row      = std::min<std::int32_t>(static_cast<std::int32_t>(glyph.height), height - top);
        const auto first_column = std::max<std::int32_t>(0, -left);
        const auto end_column   = std::min<std::int32_t>(static_cast<std::int32_t>(glyph.width), width - left);
        if (first_row >= end_row || first_column >= end_column)
            return;
        const std::int32_t   first_byte = first_column >> 3;
        const std::int32_t   last_byte  = (end_column - 1) >> 3;
        const std::uint32_t  first_mask = 0xFFU >> (first_column & 7);
        const std::uint32_t  last_mask  = (0xFF00U >> (((end_column - 1) & 7) + 1)) & 0xFFU;
        const std::size_t    stride     = glyph.stride;
        const auto*          bitmap     = glyph.data;
        const std::ptrdiff_t row_bytes  = static_cast<std::ptrdiff_t>(buffer.stride_bytes);
        std::uint8_t*        row_start  = buffer.data + static_cast<std::ptrdiff_t>(top + first_row) * row_bytes;
        for (std::int32_t row = first_row; row < end_row; ++row, row_start += row_bytes)
        {
            const auto* bits = bitmap + static_cast<std::size_t>(row) * stride;
            auto*       out  = reinterpret_cast<std::uint16_t*>(row_start);
            for (std::int32_t index = first_byte; index <= last_byte; ++index)
            {
                std::uint32_t byte = static_cast<std::uint8_t>(bits[index]);
                if (index == first_byte)
                    byte &= first_mask;
                if (index == last_byte)
                    byte &= last_mask;
                const std::int32_t column = left + (index << 3);
                while (byte != 0U)
                {
                    const std::int32_t bit = first_set_column_(byte);
                    out[column + bit]      = color;
                    byte ^= 0x80U >> static_cast<std::uint32_t>(bit);
                }
            }
        }
    }

    brick::interfaces::display::IDisplayDevice& device_;
};

}  // namespace brick::core::display
