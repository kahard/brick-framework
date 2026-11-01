#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

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
            || area.height != static_cast<std::int32_t>(buffer.height)
            || !brick::interfaces::display::DisplayRect{ 0, 0, static_cast<std::int32_t>(device_.size().width),
                                                         static_cast<std::int32_t>(device_.size().height) }
                    .contains(area)
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

    /// Fills an area of the display with color, in strips as tall as the
    /// caller's pixels allow (at least one row of the area).
    Status fill_area(brick::interfaces::display::DisplayRect area, std::uint16_t color, std::uint16_t* pixels,
                     std::size_t capacity)
    {
        if (area.empty() || pixels == nullptr || capacity < static_cast<std::size_t>(area.width))
            return Status::invalid_argument;
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
        for (std::uint32_t y = 0; y < buffer.height; ++y)
        {
            auto* row = reinterpret_cast<std::uint16_t*>(buffer.data + y * buffer.stride_bytes);
            for (std::uint32_t x = 0; x < buffer.width; ++x)
                row[x] = color;
        }
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
            for (std::uint16_t row = 0; row < glyph.height; ++row)
                for (std::uint16_t column = 0; column < glyph.width; ++column)
                    if (glyph.data[row * glyph.stride + column / 8] & (0x80u >> (column & 7)))
                    {
                        const auto px = x + column;
                        const auto py = y + glyph.top + row;
                        if (px >= 0 && py >= 0 && px < static_cast<std::int32_t>(buffer.width)
                            && py < static_cast<std::int32_t>(buffer.height))
                            reinterpret_cast<std::uint16_t*>(buffer.data + py * buffer.stride_bytes)[px] = color;
                    }
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
    brick::interfaces::display::IDisplayDevice& device_;
};

}  // namespace brick::core::display
