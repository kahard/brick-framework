#pragma once

#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/IFrameBufferDisplay.h"
#include "lvgl.h"

namespace brick::platform::esp32
{

/// LVGL v9 display adapter for the portable BRICK display contract.
///
/// The first implementation intentionally uses LVGL PARTIAL/FULL flushes.
/// DIRECT mode with scan-out page flipping will be added as a separate path
/// once LVGL buffers are explicitly matched to IFrameBufferDisplay buffers.
class LvglDisplayAdapter final
{
public:
    explicit LvglDisplayAdapter(brick::interfaces::display::IDisplayDevice& device);

    lv_display_t* create(lv_display_render_mode_t render_mode, void* buffer_1, void* buffer_2, std::uint32_t buffer_size_bytes);
    lv_display_t* create_framebuffer(brick::interfaces::display::IFrameBufferDisplay& framebuffers);

private:
    static void flush_callback_(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixel_map);
    void        flush_(lv_display_t* display, const lv_area_t& area, std::uint8_t* pixel_map);

    brick::interfaces::display::IDisplayDevice&      device_;
    brick::interfaces::display::IFrameBufferDisplay* framebuffers_  = nullptr;
    void*                                            framebuffer_1_ = nullptr;
    void*                                            framebuffer_2_ = nullptr;
};

}  // namespace brick::platform::esp32
