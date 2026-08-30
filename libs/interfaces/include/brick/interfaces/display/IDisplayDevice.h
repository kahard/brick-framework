#pragma once

#include <cstddef>
#include <cstdint>

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

    virtual bool        begin()                         = 0;
    virtual DisplaySize size() const                    = 0;
    virtual PixelFormat pixel_format() const            = 0;
    virtual bool        set_rotation(Rotation rotation) = 0;

    /// Describes the conservative capabilities of this device.
    /// Drivers can override this when they expose DMA, VSYNC or scan-out
    /// buffers. The default keeps legacy devices usable as packed SPI sinks.
    virtual DisplayCapabilities capabilities() const
    {
        const auto display_size = size();
        return {
            DisplayPanelType::host, display_size, pixel_format(), static_cast<std::size_t>(display_size.width) * display_size.height * pixel_format_bytes(pixel_format()), 1, 0, false, false, false, true, false, 1, RenderMode::partial,
        };
    }

    /// Transfers one rectangular pixel buffer to the display.
    ///
    /// The implementation owns the transfer mechanism. The caller must keep
    /// buffer.data unchanged until this method returns; asynchronous backends
    /// will later expose an explicit completion contract.
    virtual bool draw_buffer(DisplayRect area, const PixelBuffer& buffer) = 0;

    /// Submits a buffer for transfer. The default implementation is synchronous.
    ///
    /// A driver may override this when it can queue the transfer. In that case
    /// the caller must wait_for_transfer_complete() before reusing buffer.data.
    virtual bool submit_buffer(DisplayRect area, const PixelBuffer& buffer) { return draw_buffer(area, buffer); }

    /// Waits until the last submitted buffer is no longer being read by the
    /// display driver. Synchronous devices are complete immediately.
    virtual bool wait_for_transfer_complete(std::uint32_t timeout_ms)
    {
        (void)timeout_ms;
        return true;
    }

    /// Waits for a display refresh boundary when the hardware exposes VSYNC.
    /// Returns false when VSYNC is not available or the timeout expires.
    virtual bool wait_for_vsync(std::uint32_t timeout_ms)
    {
        (void)timeout_ms;
        return false;
    }

    // Coordinates use the currently selected rotation and are inclusive at the
    // start, exclusive at the end, matching common embedded display APIs.
};

}  // namespace brick::interfaces::display
