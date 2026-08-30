#pragma once

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/WritablePixelBuffer.h"

namespace brick::core::display
{

/// Small, device-independent facade for the common display operations used by
/// applications.  Low-level drivers remain available through IDisplayDevice.
class Screen final
{
public:
    explicit Screen(brick::interfaces::display::IDisplayDevice& device) : device_(device) {}

    brick::interfaces::display::DisplaySize size() const { return device_.size(); }

    bool draw(brick::interfaces::display::DisplayRect area,
              const brick::interfaces::display::PixelBuffer& buffer)
    {
        if (area.empty() || area.width != static_cast<std::int32_t>(buffer.width) ||
            area.height != static_cast<std::int32_t>(buffer.height) ||
            !brick::interfaces::display::DisplayRect{
                0, 0, static_cast<std::int32_t>(device_.size().width),
                static_cast<std::int32_t>(device_.size().height)}.contains(area) ||
            buffer.format != device_.pixel_format())
        {
            return false;
        }
        return device_.draw_buffer(area, buffer);
    }

    bool present(const brick::interfaces::display::PixelBuffer& buffer)
    {
        const auto display_size = device_.size();
        return draw({0, 0, static_cast<std::int32_t>(display_size.width),
                     static_cast<std::int32_t>(display_size.height)}, buffer);
    }

    bool fill(brick::interfaces::display::WritablePixelBuffer& buffer,
              std::uint16_t color) const
    {
        if (buffer.data == nullptr || buffer.format != brick::interfaces::display::PixelFormat::rgb565)
            return false;
        for (std::uint32_t y = 0; y < buffer.height; ++y)
        {
            auto* row = reinterpret_cast<std::uint16_t*>(buffer.data + y * buffer.stride_bytes);
            for (std::uint32_t x = 0; x < buffer.width; ++x) row[x] = color;
        }
        return true;
    }

    /// Renders a 1-bit bitmap font into an RGB565 buffer.  The glyph type is
    /// intentionally structural so generated application fonts can be used
    /// without a framework dependency.
    template <typename Glyph>
    bool draw_text(brick::interfaces::display::WritablePixelBuffer& buffer,
                   std::int32_t x, std::int32_t y, const char* text,
                   const char* characters, const Glyph* glyphs, std::size_t count,
                   std::uint16_t color) const
    {
        if (buffer.data == nullptr || text == nullptr || characters == nullptr || glyphs == nullptr ||
            buffer.format != brick::interfaces::display::PixelFormat::rgb565)
            return false;
        for (const char* cursor = text; *cursor != '\0'; ++cursor)
        {
            std::size_t index = 0;
            while (index < count && characters[index] != *cursor) ++index;
            if (index >= count) continue;
            const Glyph& glyph = glyphs[index];
            for (std::uint16_t row = 0; row < glyph.height; ++row)
                for (std::uint16_t column = 0; column < glyph.width; ++column)
                    if (glyph.data[row * glyph.stride + column / 8] & (0x80u >> (column & 7)))
                    {
                        const auto px = x + column;
                        const auto py = y + row;
                        if (px >= 0 && py >= 0 && px < static_cast<std::int32_t>(buffer.width) &&
                            py < static_cast<std::int32_t>(buffer.height))
                            reinterpret_cast<std::uint16_t*>(buffer.data + py * buffer.stride_bytes)[px] = color;
                    }
            x += glyph.width + 1;
            if (x >= static_cast<std::int32_t>(buffer.width)) break;
        }
        return true;
    }

    bool wait_for_transfer_complete(std::uint32_t timeout_ms)
    {
        return device_.wait_for_transfer_complete(timeout_ms);
    }

private:
    brick::interfaces::display::IDisplayDevice& device_;
};

}  // namespace brick::core::display
