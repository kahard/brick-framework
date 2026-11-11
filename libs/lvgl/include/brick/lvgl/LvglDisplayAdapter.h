#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/display/DisplayRect.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/IFrameBufferDisplay.h"
#include "lvgl.h"

namespace brick::lvgl
{

/// LVGL v9 display adapter for the portable BRICK display contract.
///
/// Flushes are asynchronous: the flush callback only submits the area, and
/// LVGL's flush-wait callback waits for the transfer when LVGL needs the
/// buffer again. With two draw buffers LVGL renders the next area while the
/// display (DMA) still sends the previous one; with one buffer it at least
/// prepares the next refresh meanwhile.
///
/// create_framebuffer() renders straight into the display's scan-out frame
/// buffers (no copy): every refresh ends with one page flip. In DIRECT mode
/// the wait for the VSYNC of that flip is deferred to the next time LVGL
/// draws; in FULL mode LVGL would draw into the buffer still on the screen,
/// so the flush waits for it.
class LvglDisplayAdapter final
{
public:
    explicit LvglDisplayAdapter(brick::interfaces::display::IDisplayDevice& device);

    lv_display_t* create(lv_display_render_mode_t render_mode, void* buffer_1, void* buffer_2,
                         std::uint32_t buffer_size_bytes);
    /// render_mode is LV_DISPLAY_RENDER_MODE_DIRECT (redraws only the changed
    /// areas, LVGL keeps both buffers in sync) or LV_DISPLAY_RENDER_MODE_FULL
    /// (redraws every frame completely). nullptr for other modes and for
    /// displays with fewer than two frame buffers.
    lv_display_t* create_framebuffer(brick::interfaces::display::IFrameBufferDisplay& framebuffers,
                                     lv_display_render_mode_t render_mode = LV_DISPLAY_RENDER_MODE_DIRECT);
    /// Tear-free like DIRECT, but LVGL draws in two buffers in fast RAM
    /// (PARTIAL mode) instead of the frame buffers, which are often in slower
    /// PSRAM: each area goes into the frame buffer off the screen
    /// (copy_to_frame_buffer(), a DMA copy where the display has one), the
    /// frame ends with one page flip, and the next frame first brings the
    /// areas it does not redraw over into the other frame buffer. nullptr for
    /// displays with fewer than two frame buffers or without those copies.
    lv_display_t* create_buffered(brick::interfaces::display::IFrameBufferDisplay& framebuffers, void* buffer_1,
                                  void* buffer_2, std::uint32_t buffer_size_bytes);

    /// Status of the last flush. LVGL's flush callback cannot report a failed
    /// transfer, so it is kept here for diagnostics; the next frame redraws.
    Status flush_status() const { return flush_status_; }

private:
    /// What the flush-wait callback still has to wait for.
    enum class Pending : std::uint8_t
    {
        none,
        transfer,
        vsync,
    };

    static void flush_callback_(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixel_map);
    static void flush_wait_callback_(lv_display_t* display);
    void        flush_(lv_display_t* display, const lv_area_t& area, std::uint8_t* pixel_map);
    void        present_framebuffer_(lv_display_t* display, std::uint8_t* pixel_map);
    void        flush_buffered_(lv_display_t* display, const lv_area_t& area, std::uint8_t* pixel_map);
    void        begin_buffered_frame_(lv_display_t* display);
    void        wait_for_flush_();

    /// Areas of one frame (LVGL's own limit for a refresh, LV_INV_BUF_SIZE).
    static constexpr std::size_t kMaxFrameAreas = 32;
    /// Pieces left of the last frame's areas once this frame's are cut out
    /// (each cut splits an area into up to four).
    static constexpr std::size_t kMaxSyncAreas = 128;
    struct FrameAreas
    {
        std::array<lv_area_t, kMaxFrameAreas> areas{};
        std::size_t                           count = 0;
    };

    brick::interfaces::display::IDisplayDevice&      device_;
    brick::interfaces::display::IFrameBufferDisplay* framebuffers_  = nullptr;
    void*                                            framebuffer_1_ = nullptr;
    void*                                            framebuffer_2_ = nullptr;
    Status                                           flush_status_  = Status::ok;
    Pending                                          pending_       = Pending::none;
    // create_buffered(): which frame buffer is off the screen, whether a
    // frame is being flushed, what the last frame redrew and (scratch) what
    // of it the frame buffer off the screen still lacks.
    bool                                 buffered_   = false;
    std::uint8_t                         back_       = 1;
    bool                                 frame_open_ = false;
    FrameAreas                           last_frame_;
    std::array<lv_area_t, kMaxSyncAreas> sync_areas_{};
};

}  // namespace brick::lvgl
