#pragma once

#include "brick/interfaces/display/IDisplayDevice.h"

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

    bool wait_for_transfer_complete(std::uint32_t timeout_ms)
    {
        return device_.wait_for_transfer_complete(timeout_ms);
    }

private:
    brick::interfaces::display::IDisplayDevice& device_;
};

}  // namespace brick::core::display
