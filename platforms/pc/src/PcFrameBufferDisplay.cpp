#include "brick/platform/pc/PcFrameBufferDisplay.h"

#include <algorithm>
#include <cstring>

namespace brick::platform::pc
{

using brick::interfaces::display::DisplayCapabilities;
using brick::interfaces::display::DisplayPanelType;
using brick::interfaces::display::DisplayRect;
using brick::interfaces::display::DisplaySize;
using brick::interfaces::display::PixelBuffer;
using brick::interfaces::display::PixelFormat;
using brick::interfaces::display::RenderMode;
using brick::interfaces::display::Rotation;
using brick::interfaces::display::WritablePixelBuffer;

PcFrameBufferDisplay::PcFrameBufferDisplay(PcDisplayConfig config) : config_(config)
{
    resize();
}

bool PcFrameBufferDisplay::begin()
{
    started_ = config_.width != 0 && config_.height != 0;
    return started_;
}

DisplaySize PcFrameBufferDisplay::size() const
{
    const bool swapped = rotation_ == Rotation::rotate_90 || rotation_ == Rotation::rotate_270;
    return swapped ? DisplaySize{ config_.height, config_.width } : DisplaySize{ config_.width, config_.height };
}

PixelFormat PcFrameBufferDisplay::pixel_format() const
{
    return PixelFormat::rgb565;
}

bool PcFrameBufferDisplay::set_rotation(Rotation rotation)
{
    rotation_ = rotation;
    resize();
    return true;
}

DisplayCapabilities PcFrameBufferDisplay::capabilities() const
{
    // Every hardware panel driver transfers with DMA; host memory stands in for
    // DMA-capable memory in the simulator.
    const auto          display_size = size();
    const std::size_t   frame_bytes  = static_cast<std::size_t>(display_size.width) * display_size.height * 2U;
    const bool          scanout      = config_.frame_buffers != 0;
    DisplayCapabilities result{};
    result.panel_type         = scanout ? DisplayPanelType::rgb : DisplayPanelType::spi;
    result.size               = display_size;
    result.native_format      = PixelFormat::rgb565;
    result.full_frame_bytes   = frame_bytes;
    result.max_transfer_bytes = frame_bytes;
    result.dma                = true;
    result.vsync              = scanout;
    result.scanout_buffers    = scanout;
    result.partial_transfer   = true;
    result.max_buffer_count   = scanout ? config_.frame_buffers : 1;
    result.preferred_mode     = scanout ? RenderMode::direct : RenderMode::partial;
    return result;
}

bool PcFrameBufferDisplay::draw_buffer(DisplayRect area, const PixelBuffer& buffer)
{
    const auto display_size = size();
    if (!started_ || !buffer.valid() || buffer.format != PixelFormat::rgb565 || area.empty() || area.x < 0 || area.y < 0
        || area.x + area.width > display_size.width || area.y + area.height > display_size.height
        || buffer.width < static_cast<std::uint32_t>(area.width)
        || buffer.height < static_cast<std::uint32_t>(area.height))
        return false;
    for (std::int32_t row = 0; row < area.height; ++row)
    {
        const std::uint8_t* source      = buffer.data + static_cast<std::size_t>(row) * buffer.stride_bytes;
        std::uint16_t*      destination = screen_.data() + static_cast<std::size_t>(area.y + row) * display_size.width
                                     + static_cast<std::size_t>(area.x);
        std::memcpy(destination, source, static_cast<std::size_t>(area.width) * 2U);
    }
    ++frame_counter_;
    return true;
}

bool PcFrameBufferDisplay::wait_for_vsync(std::uint32_t timeout_ms)
{
    (void)timeout_ms;
    return config_.frame_buffers != 0;
}

std::uint8_t PcFrameBufferDisplay::frame_buffer_count() const
{
    return config_.frame_buffers;
}

bool PcFrameBufferDisplay::get_frame_buffer(std::uint8_t index, WritablePixelBuffer& buffer)
{
    if (index >= frame_buffers_.size())
        return false;
    const auto display_size = size();
    buffer.data             = reinterpret_cast<std::uint8_t*>(frame_buffers_[index].data());
    buffer.width            = display_size.width;
    buffer.height           = display_size.height;
    buffer.stride_bytes     = static_cast<std::size_t>(display_size.width) * 2U;
    buffer.format           = PixelFormat::rgb565;
    buffer.dma_capable      = true;
    return true;
}

bool PcFrameBufferDisplay::present_frame_buffer(std::uint8_t index)
{
    if (!started_ || index >= frame_buffers_.size())
        return false;
    screen_ = frame_buffers_[index];
    ++frame_counter_;
    return true;
}

void PcFrameBufferDisplay::resize()
{
    const auto        display_size = size();
    const std::size_t pixels       = static_cast<std::size_t>(display_size.width) * display_size.height;
    screen_.assign(pixels, 0);
    frame_buffers_.assign(config_.frame_buffers, std::vector<std::uint16_t>(pixels, 0));
}

}  // namespace brick::platform::pc
