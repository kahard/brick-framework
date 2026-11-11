#include "brick/lvgl/LvglDisplayAdapter.h"

#include <cstddef>

// The areas LVGL redraws in a refresh (lv_display_t::inv_areas) for
// create_buffered().
#include "lvgl_private.h"


namespace brick::lvgl
{

namespace
{
    constexpr std::uint32_t kTransferTimeoutMs = 1000;
    constexpr std::uint32_t kVsyncTimeoutMs    = 100;
    constexpr std::uint8_t  kNoFramebuffer     = 0xFF;
}  // namespace

LvglDisplayAdapter::LvglDisplayAdapter(brick::interfaces::display::IDisplayDevice& device) : device_(device)
{
}

lv_display_t* LvglDisplayAdapter::create(lv_display_render_mode_t render_mode, void* buffer_1, void* buffer_2,
                                         std::uint32_t buffer_size_bytes)
{
    const auto display_size = device_.size();
    auto*      display      = lv_display_create(display_size.width, display_size.height);
    if (display == nullptr)
        return nullptr;

    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, &LvglDisplayAdapter::flush_callback_);
    lv_display_set_flush_wait_cb(display, &LvglDisplayAdapter::flush_wait_callback_);
    lv_display_set_user_data(display, this);
    if (buffer_1 != nullptr && buffer_size_bytes != 0)
        lv_display_set_buffers(display, buffer_1, buffer_2, buffer_size_bytes, render_mode);
    return display;
}

lv_display_t* LvglDisplayAdapter::create_framebuffer(brick::interfaces::display::IFrameBufferDisplay& framebuffers,
                                                     lv_display_render_mode_t                         render_mode)
{
    if (framebuffers.frame_buffer_count() < 2
        || (render_mode != LV_DISPLAY_RENDER_MODE_DIRECT && render_mode != LV_DISPLAY_RENDER_MODE_FULL))
        return nullptr;

    brick::interfaces::display::WritablePixelBuffer buffer_1;
    brick::interfaces::display::WritablePixelBuffer buffer_2;
    if (!is_ok(framebuffers.get_frame_buffer(0, buffer_1)) || !is_ok(framebuffers.get_frame_buffer(1, buffer_2))
        || !buffer_1.valid() || !buffer_2.valid() || buffer_1.width != buffer_2.width
        || buffer_1.height != buffer_2.height || buffer_1.stride_bytes != buffer_2.stride_bytes)
        return nullptr;

    const auto buffer_size = static_cast<std::uint32_t>(buffer_1.stride_bytes * buffer_1.height);
    auto*      display     = create(render_mode, buffer_1.data, buffer_2.data, buffer_size);
    if (display == nullptr)
        return nullptr;

    framebuffers_  = &framebuffers;
    framebuffer_1_ = buffer_1.data;
    framebuffer_2_ = buffer_2.data;
    return display;
}

lv_display_t* LvglDisplayAdapter::create_buffered(brick::interfaces::display::IFrameBufferDisplay& framebuffers,
                                                  void* buffer_1, void* buffer_2, std::uint32_t buffer_size_bytes)
{
    // An empty copy tells whether the display has the copies.
    if (framebuffers.frame_buffer_count() < 2
        || framebuffers.copy_between_frame_buffers(0, 1, {}) == Status::not_supported)
        return nullptr;
    auto* display = create(LV_DISPLAY_RENDER_MODE_PARTIAL, buffer_1, buffer_2, buffer_size_bytes);
    if (display == nullptr)
        return nullptr;
    framebuffers_ = &framebuffers;
    buffered_     = true;
    back_         = 1;
    // Frame buffer 0 is on the screen from the start.
    if (!is_ok(framebuffers.present_frame_buffer(0)))
        return nullptr;
    pending_ = Pending::vsync;
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

void LvglDisplayAdapter::flush_wait_callback_(lv_display_t* display)
{
    // LVGL marks the flush finished when this returns.
    auto* adapter = display != nullptr ? static_cast<LvglDisplayAdapter*>(lv_display_get_user_data(display)) : nullptr;
    if (adapter != nullptr)
        adapter->wait_for_flush_();
}

void LvglDisplayAdapter::flush_(lv_display_t* display, const lv_area_t& area, std::uint8_t* pixel_map)
{
    if (buffered_)
    {
        flush_buffered_(display, area, pixel_map);
        return;
    }
    if (framebuffers_ != nullptr)
    {
        present_framebuffer_(display, pixel_map);
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
        pixel_map,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        static_cast<std::size_t>(width) * 2,
        brick::interfaces::display::PixelFormat::rgb565,
        false,
    };
    const brick::interfaces::display::DisplayRect rectangle{
        area.x1,
        area.y1,
        width,
        height,
    };

    // The transfer runs on; wait_for_flush_() completes it when LVGL needs
    // this buffer again.
    flush_status_ = device_.submit_buffer(rectangle, buffer);
    if (is_ok(flush_status_))
        pending_ = Pending::transfer;
    else
        lv_display_flush_ready(display);
}

void LvglDisplayAdapter::present_framebuffer_(lv_display_t* display, std::uint8_t* pixel_map)
{
    // In DIRECT mode LVGL flushes every changed area of the frame buffer; the
    // frame is complete, and can be shown, with the last one.
    std::uint8_t index = kNoFramebuffer;
    if (pixel_map == framebuffer_1_)
        index = 0;
    else if (pixel_map == framebuffer_2_)
        index = 1;
    if (!lv_display_flush_is_last(display) || index == kNoFramebuffer)
    {
        lv_display_flush_ready(display);
        return;
    }
    flush_status_ = framebuffers_->present_frame_buffer(index);
    if (!is_ok(flush_status_))
    {
        lv_display_flush_ready(display);
        return;
    }
    pending_ = Pending::vsync;
    // DIRECT mode waits for the flush before it touches the other buffer at
    // the next refresh, so the VSYNC wait can wait until then. In FULL mode
    // LVGL starts the next frame in the other buffer right away, and that one
    // is on the screen until the flip: wait now.
    if (lv_display_get_render_mode(display) != LV_DISPLAY_RENDER_MODE_DIRECT)
    {
        wait_for_flush_();
        lv_display_flush_ready(display);
    }
}

void LvglDisplayAdapter::flush_buffered_(lv_display_t* display, const lv_area_t& area, std::uint8_t* pixel_map)
{
    if (!frame_open_)
        begin_buffered_frame_(display);
    const auto                                    width  = area.x2 - area.x1 + 1;
    const auto                                    height = area.y2 - area.y1 + 1;
    const brick::interfaces::display::PixelBuffer pixels{
        pixel_map,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        static_cast<std::size_t>(width) * 2,
        brick::interfaces::display::PixelFormat::rgb565,
        true,
    };
    // Into the frame buffer off the screen. The copy may run on (DMA) while
    // LVGL draws the next area into its other buffer: the flush-wait
    // callback waits for it.
    const Status copied = framebuffers_->copy_to_frame_buffer(back_, { area.x1, area.y1, width, height }, pixels);
    if (!is_ok(copied))
        flush_status_ = copied;
    pending_ = Pending::transfer;
    if (!lv_display_flush_is_last(display))
        return;
    // The frame is complete once the last copy is.
    wait_for_flush_();
    if (const Status status = framebuffers_->present_frame_buffer(back_); !is_ok(status))
        flush_status_ = status;
    pending_    = Pending::vsync;
    back_       = static_cast<std::uint8_t>(back_ ^ 1U);
    frame_open_ = false;
}

void LvglDisplayAdapter::begin_buffered_frame_(lv_display_t* display)
{
    // Until the last flip has happened the off-screen buffer is still shown.
    wait_for_flush_();
    // What LVGL redraws in this frame.
    FrameAreas this_frame;
    for (std::uint32_t index = 0; index < display->inv_p && this_frame.count < kMaxFrameAreas; ++index)
        if (display->inv_area_joined[index] == 0U)
            this_frame.areas[this_frame.count++] = display->inv_areas[index];
    // The off-screen buffer holds the frame before last: it lacks what the
    // last frame changed, less what this frame redraws anyway. As LVGL's own
    // DIRECT-mode sync, every redrawn area is cut out of the last frame's
    // areas, which leaves the strips that moved objects uncovered.
    std::size_t count = 0;
    for (std::size_t index = 0; index < last_frame_.count; ++index)
        sync_areas_[count++] = last_frame_.areas[index];
    for (std::size_t redrawn = 0; redrawn < this_frame.count; ++redrawn)
    {
        std::size_t index = 0;
        while (index < count)
        {
            lv_area_t    pieces[4];
            const int8_t piece_count = lv_area_diff(pieces, &sync_areas_[index], &this_frame.areas[redrawn]);
            // Untouched, or no room for the pieces: kept as it is.
            if (piece_count < 0 || count - 1 + static_cast<std::size_t>(piece_count) > kMaxSyncAreas)
            {
                ++index;
                continue;
            }
            // Replaced by its pieces, which lie outside this redrawn area.
            sync_areas_[index] = sync_areas_[--count];
            for (int8_t piece = 0; piece < piece_count; ++piece)
                sync_areas_[count++] = pieces[piece];
        }
    }
    // The copies may run on (DMA); they land before this frame's own.
    const std::uint8_t front = static_cast<std::uint8_t>(back_ ^ 1U);
    for (std::size_t index = 0; index < count; ++index)
    {
        const lv_area_t& area = sync_areas_[index];
        if (const Status status = framebuffers_->copy_between_frame_buffers(
                front, back_, { area.x1, area.y1, area.x2 - area.x1 + 1, area.y2 - area.y1 + 1 });
            !is_ok(status))
            flush_status_ = status;
        pending_ = Pending::transfer;
    }
    last_frame_ = this_frame;
    frame_open_ = true;
}

void LvglDisplayAdapter::wait_for_flush_()
{
    Status status = Status::ok;
    switch (pending_)
    {
        case Pending::transfer:
            status = device_.wait_for_transfer_complete(kTransferTimeoutMs);
            break;
        case Pending::vsync:
            // Until the flip, the previous buffer is still on the screen.
            status = device_.wait_for_vsync(kVsyncTimeoutMs);
            if (status == Status::not_supported)
                status = Status::ok;
            break;
        case Pending::none:
            break;
    }
    pending_ = Pending::none;
    if (!is_ok(status))
        flush_status_ = status;
}

}  // namespace brick::lvgl
