#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/DisplayTypes.h"

namespace brick::interfaces::display
{

enum class DisplayPanelType : std::uint8_t
{
    spi,
    rgb,
    mipi_dsi,
    ltdc,
    host,
};

enum class RenderMode : std::uint8_t
{
    partial,
    direct,
    full,
};

struct DisplayCapabilities
{
    DisplayPanelType panel_type          = DisplayPanelType::host;
    DisplaySize      size                = {};
    PixelFormat      native_format       = PixelFormat::rgb565;
    std::size_t      full_frame_bytes    = 0;
    std::size_t      dma_alignment_bytes = 1;
    std::size_t      max_transfer_bytes  = 0;
    bool             dma                 = false;
    bool             vsync               = false;
    bool             scanout_buffers     = false;
    bool             partial_transfer    = false;
    /// submit_buffer() returns while the display still reads the caller's
    /// buffer; wait_for_transfer_complete() releases it. A caller with two
    /// buffers can then fill one while the other is sent. A driver that
    /// copies the pixels before returning (and sends its copy in the
    /// background) reports false: the caller's buffer is free at once.
    bool         async_transfer   = false;
    std::uint8_t max_buffer_count = 1;
    RenderMode   preferred_mode   = RenderMode::partial;
};

}  // namespace brick::interfaces::display
