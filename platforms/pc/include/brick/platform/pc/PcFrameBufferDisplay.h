#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "brick/Status.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/IFrameBufferDisplay.h"

namespace brick::platform::pc
{

struct PcDisplayConfig
{
    std::uint16_t width  = 320;  ///< native width (rotation 0)
    std::uint16_t height = 240;  ///< native height (rotation 0)
    /// 0 emulates a partial-update panel (SPI, parallel); 1-3 emulates a panel
    /// with scan-out frame buffers (RGB, MIPI-DSI) exposed via IFrameBufferDisplay.
    std::uint8_t frame_buffers = 0;
};

/// RGB565 display kept in memory. Whatever the application draws ends up in
/// screen(), which the simulator window and screenshots read.
class PcFrameBufferDisplay final : public brick::interfaces::display::IDisplayDevice,
                                   public brick::interfaces::display::IFrameBufferDisplay
{
public:
    explicit PcFrameBufferDisplay(PcDisplayConfig config);

    Status                                  begin() override;
    brick::interfaces::display::DisplaySize size() const override;
    brick::interfaces::display::PixelFormat pixel_format() const override;
    Status                                  set_rotation(brick::interfaces::display::Rotation rotation) override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    Status                                          draw_buffer(brick::interfaces::display::DisplayRect        area,
                                                                const brick::interfaces::display::PixelBuffer& buffer) override;
    Status                                          wait_for_vsync(std::uint32_t timeout_ms) override;

    std::uint8_t frame_buffer_count() const override;
    Status       get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer) override;
    Status       present_frame_buffer(std::uint8_t index) override;

    /// Visible image, size().width * size().height RGB565 pixels, row-major.
    const std::uint16_t* screen() const { return screen_.data(); }
    /// Incremented whenever the visible image changes.
    std::uint32_t frame_counter() const { return frame_counter_; }
    bool          started() const { return started_; }

private:
    void resize();

    PcDisplayConfig                         config_;
    brick::interfaces::display::Rotation    rotation_ = brick::interfaces::display::Rotation::rotate_0;
    std::vector<std::uint16_t>              screen_;
    std::vector<std::vector<std::uint16_t>> frame_buffers_;
    std::uint32_t                           frame_counter_ = 0;
    bool                                    started_       = false;
};

}  // namespace brick::platform::pc
