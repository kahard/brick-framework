#include "brick/platform/esp32/LvglDisplayAdapter.h"

#include <cstddef>

#include "esp_log.h"

namespace brick::platform::esp32
{

LvglDisplayAdapter::LvglDisplayAdapter(brick::interfaces::display::IDisplayDevice& device) : device_(device)
{
}

lv_display_t* LvglDisplayAdapter::create(lv_display_render_mode_t render_mode, void* buffer_1, void* buffer_2, std::uint32_t buffer_size_bytes)
{
    const auto display_size = device_.size();
    auto*      display      = lv_display_create(display_size.width, display_size.height);
    if (display == nullptr)
        return nullptr;

    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, &LvglDisplayAdapter::flush_callback_);
    lv_display_set_user_data(display, this);
    if (buffer_1 != nullptr && buffer_size_bytes != 0)
        lv_display_set_buffers(display, buffer_1, buffer_2, buffer_size_bytes, render_mode);
    return display;
}

lv_display_t* LvglDisplayAdapter::create_framebuffer(brick::interfaces::display::IFrameBufferDisplay& framebuffers)
{
    if (framebuffers.frame_buffer_count() < 2)
        return nullptr;

    brick::interfaces::display::WritablePixelBuffer buffer_1;
    brick::interfaces::display::WritablePixelBuffer buffer_2;
    if (!framebuffers.get_frame_buffer(0, buffer_1) || !framebuffers.get_frame_buffer(1, buffer_2) || !buffer_1.valid() || !buffer_2.valid() || buffer_1.width != buffer_2.width || buffer_1.height != buffer_2.height
        || buffer_1.stride_bytes != buffer_2.stride_bytes)
        return nullptr;

    const auto buffer_size = static_cast<std::uint32_t>(buffer_1.stride_bytes * buffer_1.height);
    auto*      display     = create(LV_DISPLAY_RENDER_MODE_DIRECT, buffer_1.data, buffer_2.data, buffer_size);
    if (display == nullptr)
        return nullptr;

    framebuffers_  = &framebuffers;
    framebuffer_1_ = buffer_1.data;
    framebuffer_2_ = buffer_2.data;
    return display;
}

void LvglDisplayAdapter::flush_callback_(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixel_map)
{
    if (display == nullptr || area == nullptr || pixel_map == nullptr)
    {
        if (display != nullptr)
            lv_display_flush_ready(display);
        return;
    }

    auto* adapter = static_cast<LvglDisplayAdapter*>(lv_display_get_user_data(display));
    if (adapter == nullptr)
    {
        lv_display_flush_ready(display);
        return;
    }
    adapter->flush_(display, *area, pixel_map);
}

void LvglDisplayAdapter::flush_(lv_display_t* display, const lv_area_t& area, std::uint8_t* pixel_map)
{
    if (framebuffers_ != nullptr)
    {
        std::uint8_t index = 0xFF;
        if (pixel_map == framebuffer_1_)
            index = 0;
        else if (pixel_map == framebuffer_2_)
            index = 1;

        if (index != 0xFF)
        {
            framebuffers_->present_frame_buffer(index);
            device_.wait_for_vsync(100);
        }
        lv_display_flush_ready(display);
        return;
    }

    const auto width  = area.x2 - area.x1 + 1;
    const auto height = area.y2 - area.y1 + 1;
    if (area.x1 < 0 || area.y1 < 0 || width <= 0 || height <= 0)
    {
        lv_display_flush_ready(display);
        return;
    }

    const brick::interfaces::display::PixelBuffer buffer{
        pixel_map, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), static_cast<std::size_t>(width) * 2, brick::interfaces::display::PixelFormat::rgb565, false,
    };
    const brick::interfaces::display::DisplayRect rectangle{
        area.x1,
        area.y1,
        width,
        height,
    };

    if (device_.submit_buffer(rectangle, buffer))
        device_.wait_for_transfer_complete(1000);
    lv_display_flush_ready(display);
}

}  // namespace brick::platform::esp32
