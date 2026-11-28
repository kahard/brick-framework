#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/IFrameBufferDisplay.h"
#include "brick/platform/stm32/h7/Gpio.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

/// Panel timing in pixel clocks and lines.
struct LtdcTiming
{
    std::uint16_t hsync_width       = 4;
    std::uint16_t hsync_back_porch  = 10;
    std::uint16_t hsync_front_porch = 40;
    std::uint16_t vsync_width       = 4;
    std::uint16_t vsync_back_porch  = 10;
    std::uint16_t vsync_front_porch = 10;
    bool          hsync_active_high = false;
    bool          vsync_active_high = false;
    bool          de_active_high    = false;
    bool          pclk_inverted     = false;
};

/// The pixel clock from PLL3 R.
struct LtdcClock
{
    std::uint32_t pll3_m           = 1;
    std::uint32_t pll3_n           = 48;
    std::uint32_t pll3_p           = 8;
    std::uint32_t pll3_q           = 8;
    std::uint32_t pll3_r           = 16;
    std::uint32_t pll3_input_range = RCC_PLL3VCIRANGE_3;
};

struct LtdcDisplayConfig
{
    static constexpr std::size_t kMaxFrameBuffers = 3;

    std::uint16_t width  = 0;
    std::uint16_t height = 0;
    LtdcTiming    timing;
    LtdcClock     clock;

    const AlternatePin* pins      = nullptr;  ///< data, clock, sync and DE pins
    std::size_t         pin_count = 0;
    /// The panel's display-on input (DISP), driven high once the LTDC runs.
    GpioPin display_enable;
    /// Enable of the backlight LED driver (full brightness while on).
    GpioPin backlight_enable;
    bool    backlight_active_high = true;

    /// RGB565 frame buffers of width * height pixels, in memory the LTDC can
    /// read (external SDRAM or AXI SRAM), owned by the board.
    std::array<std::uint16_t*, kMaxFrameBuffers> frame_buffers{};
    std::uint8_t                                 frame_buffer_count = 1;
};

/// RGB panel on the LTDC, one RGB565 layer showing one of the frame buffers.
/// With two buffers the application draws the one off the screen and
/// present_frame_buffer() shows it at the next vertical blanking; with three
/// it need not wait for that before it draws the next one;
/// wait_for_vsync() returns once it is on the screen. draw_buffer() and the
/// copies are CPU copies into a frame buffer.
class LtdcDisplay final : public brick::interfaces::display::IDisplayDevice,
                          public brick::interfaces::display::IFrameBufferDisplay
{
public:
    explicit LtdcDisplay(LtdcDisplayConfig config) : config_(config) {}

    Status                                          begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    Status set_rotation(brick::interfaces::display::Rotation rotation) override;
    Status draw_buffer(brick::interfaces::display::DisplayRect        area,
                       const brick::interfaces::display::PixelBuffer& buffer) override;
    Status wait_for_vsync(std::uint32_t timeout_ms) override;

    std::uint8_t frame_buffer_count() const override;
    Status       get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer) override;
    Status       present_frame_buffer(std::uint8_t index) override;

    /// CPU fill and copies into the frame buffers (finished on return).
    Status fill_rect(brick::interfaces::display::DisplayRect area, std::uint16_t color) override;
    Status copy_to_frame_buffer(std::uint8_t index, brick::interfaces::display::DisplayRect area,
                                const brick::interfaces::display::PixelBuffer& pixels) override;
    Status copy_between_frame_buffers(std::uint8_t from, std::uint8_t to,
                                      brick::interfaces::display::DisplayRect area) override;

    /// Backlight on or off (the enable pin).
    void set_backlight(bool on);

    /// Changes the blanking after the picture (the vertical front porch, in
    /// lines) while the display runs. With the pixel clock as it is that
    /// sets the refresh rate: more lines, fewer refreshes a second (800x480
    /// at 24 MHz and 854 clocks a line: 10 lines 55.8 Hz, 68 lines 50 Hz).
    /// The panel must take the longer blanking; lines 0 gives invalid_argument.
    Status        set_vsync_front_porch(std::uint16_t lines);
    std::uint16_t vsync_front_porch() const { return config_.timing.vsync_front_porch; }

private:
    bool   inside_(brick::interfaces::display::DisplayRect area) const;
    Status copy_rows_(std::uint8_t index, brick::interfaces::display::DisplayRect area,
                      const brick::interfaces::display::PixelBuffer& pixels);

    LtdcDisplayConfig  config_;
    LTDC_HandleTypeDef handle_{};
    std::uint8_t       shown_        = 0;
    bool               flip_pending_ = false;  ///< presented, not yet waited for
    bool               started_      = false;
};

}  // namespace brick::platform::stm32::h7
