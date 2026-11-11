#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/display/DisplayCapabilities.h"
#include "brick/interfaces/display/DisplayRect.h"
#include "brick/interfaces/display/DisplayTypes.h"
#include "brick/interfaces/display/PixelBuffer.h"

namespace brick::interfaces::display
{

class IDisplayDevice
{
public:
    virtual ~IDisplayDevice() = default;

    virtual Status      begin()                         = 0;
    virtual DisplaySize size() const                    = 0;
    virtual PixelFormat pixel_format() const            = 0;
    virtual Status      set_rotation(Rotation rotation) = 0;

    /// Describes the conservative capabilities of this device.
    /// Drivers can override this when they expose DMA, VSYNC or scan-out
    /// buffers. The default keeps legacy devices usable as packed SPI sinks.
    virtual DisplayCapabilities capabilities() const
    {
        const auto display_size = size();
        return {
            DisplayPanelType::host,
            display_size,
            pixel_format(),
            static_cast<std::size_t>(display_size.width) * display_size.height * pixel_format_bytes(pixel_format()),
            1,
            0,
            false,
            false,
            false,
            true,
            false,
            1,
            RenderMode::partial,
        };
    }

    /// Transfers one rectangular pixel buffer to the display.
    ///
    /// The implementation owns the transfer mechanism. The caller must keep
    /// buffer.data unchanged until this method returns; asynchronous backends
    /// will later expose an explicit completion contract.
    virtual Status draw_buffer(DisplayRect area, const PixelBuffer& buffer) = 0;

    /// Submits a buffer for transfer. The default implementation is synchronous.
    ///
    /// A driver may override this when it can queue the transfer. In that case
    /// the caller must wait_for_transfer_complete() before reusing buffer.data.
    /// Meanwhile the caller can prepare the next buffer; that overlap is what
    /// DisplayCapabilities::async_transfer announces.
    virtual Status submit_buffer(DisplayRect area, const PixelBuffer& buffer) { return draw_buffer(area, buffer); }

    /// Waits until the last submitted buffer is no longer being read by the
    /// display driver. Synchronous devices are complete immediately, and so is
    /// a driver that has already copied the pixels into its own transfer
    /// buffers: the panel may still be receiving them, and the driver finishes
    /// that before its next operation. Calling it with nothing submitted
    /// returns Status::ok.
    virtual Status wait_for_transfer_complete(std::uint32_t timeout_ms)
    {
        (void)timeout_ms;
        return Status::ok;
    }

    /// Fills area with one RGB565 color without a caller buffer, for example
    /// with a DMA fill engine or by sending one prepared transfer buffer
    /// repeatedly. Status::not_supported when the device has no such path; the
    /// caller then draws the color from its own pixels, as
    /// brick::core::display::Screen::fill_area() does. Like submit_buffer(),
    /// the fill may still be running on return.
    virtual Status fill_rect(DisplayRect area, std::uint16_t color)
    {
        (void)area;
        (void)color;
        return Status::not_supported;
    }

    /// Waits for a display refresh boundary when the hardware exposes VSYNC.
    /// Status::not_supported when the device has no VSYNC, Status::timeout when
    /// the timeout expires.
    virtual Status wait_for_vsync(std::uint32_t timeout_ms)
    {
        (void)timeout_ms;
        return Status::not_supported;
    }

    // Coordinates use the currently selected rotation and are inclusive at the
    // start, exclusive at the end, matching common embedded display APIs.
};

}  // namespace brick::interfaces::display
