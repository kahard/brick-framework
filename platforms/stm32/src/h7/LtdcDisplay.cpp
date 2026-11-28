#include "brick/platform/stm32/h7/LtdcDisplay.h"

#include <algorithm>
#include <cstring>

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

namespace
{
    constexpr std::size_t   kPixelBytes        = 2;
    constexpr std::uint32_t kBlankingTimeoutMs = 100;  // several frames: the picture's lines come every one

    using brick::interfaces::display::DisplayRect;
    using brick::interfaces::display::PixelBuffer;
    using brick::interfaces::display::PixelFormat;
}  // namespace

Status LtdcDisplay::begin()
{
    if (started_)
        return Status::ok;
    if (config_.width == 0 || config_.height == 0 || config_.pins == nullptr || config_.frame_buffer_count == 0
        || config_.frame_buffer_count > LtdcDisplayConfig::kMaxFrameBuffers)
        return Status::invalid_argument;
    for (std::uint8_t index = 0; index < config_.frame_buffer_count; ++index)
        if (config_.frame_buffers[index] == nullptr)
            return Status::invalid_argument;

    configure(config_.backlight_enable, GPIO_MODE_OUTPUT_PP);
    set_backlight(false);
    configure(config_.display_enable, GPIO_MODE_OUTPUT_PP);
    write(config_.display_enable, false);
    configure(config_.pins, config_.pin_count, GPIO_SPEED_FREQ_MEDIUM);
    const std::size_t pixels = static_cast<std::size_t>(config_.width) * config_.height;
    for (std::uint8_t index = 0; index < config_.frame_buffer_count; ++index)
        std::fill_n(config_.frame_buffers[index], pixels, std::uint16_t{ 0 });

    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    clock.PLL3.PLL3M           = config_.clock.pll3_m;
    clock.PLL3.PLL3N           = config_.clock.pll3_n;
    clock.PLL3.PLL3P           = config_.clock.pll3_p;
    clock.PLL3.PLL3Q           = config_.clock.pll3_q;
    clock.PLL3.PLL3R           = config_.clock.pll3_r;
    clock.PLL3.PLL3RGE         = config_.clock.pll3_input_range;
    clock.PLL3.PLL3VCOSEL      = RCC_PLL3VCOWIDE;
    clock.PLL3.PLL3FRACN       = 0;
    if (const HAL_StatusTypeDef result = HAL_RCCEx_PeriphCLKConfig(&clock); result != HAL_OK)
        return to_status(result);
    __HAL_RCC_LTDC_CLK_ENABLE();

    const LtdcTiming& timing        = config_.timing;
    handle_.Instance                = LTDC;
    handle_.Init.HSPolarity         = timing.hsync_active_high ? LTDC_HSPOLARITY_AH : LTDC_HSPOLARITY_AL;
    handle_.Init.VSPolarity         = timing.vsync_active_high ? LTDC_VSPOLARITY_AH : LTDC_VSPOLARITY_AL;
    handle_.Init.DEPolarity         = timing.de_active_high ? LTDC_DEPOLARITY_AH : LTDC_DEPOLARITY_AL;
    handle_.Init.PCPolarity         = timing.pclk_inverted ? LTDC_PCPOLARITY_IIPC : LTDC_PCPOLARITY_IPC;
    handle_.Init.HorizontalSync     = timing.hsync_width - 1U;
    handle_.Init.VerticalSync       = timing.vsync_width - 1U;
    handle_.Init.AccumulatedHBP     = timing.hsync_width + timing.hsync_back_porch - 1U;
    handle_.Init.AccumulatedVBP     = timing.vsync_width + timing.vsync_back_porch - 1U;
    handle_.Init.AccumulatedActiveW = timing.hsync_width + timing.hsync_back_porch + config_.width - 1U;
    handle_.Init.AccumulatedActiveH = timing.vsync_width + timing.vsync_back_porch + config_.height - 1U;
    handle_.Init.TotalWidth =
        timing.hsync_width + timing.hsync_back_porch + config_.width + timing.hsync_front_porch - 1U;
    handle_.Init.TotalHeigh =
        timing.vsync_width + timing.vsync_back_porch + config_.height + timing.vsync_front_porch - 1U;
    handle_.Init.Backcolor.Blue  = 0;
    handle_.Init.Backcolor.Green = 0;
    handle_.Init.Backcolor.Red   = 0;
    if (const HAL_StatusTypeDef result = HAL_LTDC_Init(&handle_); result != HAL_OK)
        return to_status(result);

    LTDC_LayerCfgTypeDef layer{};
    layer.WindowX0        = 0;
    layer.WindowX1        = config_.width;
    layer.WindowY0        = 0;
    layer.WindowY1        = config_.height;
    layer.PixelFormat     = LTDC_PIXEL_FORMAT_RGB565;
    layer.Alpha           = 255;
    layer.Alpha0          = 0;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    layer.FBStartAdress   = reinterpret_cast<std::uint32_t>(config_.frame_buffers[0]);
    layer.ImageWidth      = config_.width;
    layer.ImageHeight     = config_.height;
    if (const HAL_StatusTypeDef result = HAL_LTDC_ConfigLayer(&handle_, &layer, LTDC_LAYER_1); result != HAL_OK)
        return to_status(result);

    shown_   = 0;
    started_ = true;
    write(config_.display_enable, true);
    set_backlight(true);
    return Status::ok;
}

brick::interfaces::display::DisplaySize LtdcDisplay::size() const
{
    return { config_.width, config_.height };
}

brick::interfaces::display::PixelFormat LtdcDisplay::pixel_format() const
{
    return PixelFormat::rgb565;
}

brick::interfaces::display::DisplayCapabilities LtdcDisplay::capabilities() const
{
    return {
        brick::interfaces::display::DisplayPanelType::ltdc,
        size(),
        pixel_format(),
        static_cast<std::size_t>(config_.width) * config_.height * kPixelBytes,
        4,
        0,
        false,
        true,
        config_.frame_buffer_count > 1,
        true,
        false,
        config_.frame_buffer_count,
        config_.frame_buffer_count > 1 ? brick::interfaces::display::RenderMode::direct :
                                         brick::interfaces::display::RenderMode::partial,
    };
}

Status LtdcDisplay::set_rotation(brick::interfaces::display::Rotation rotation)
{
    return rotation == brick::interfaces::display::Rotation::rotate_0 ? Status::ok : Status::not_supported;
}

Status LtdcDisplay::draw_buffer(DisplayRect area, const PixelBuffer& buffer)
{
    return copy_to_frame_buffer(shown_, area, buffer);
}

Status LtdcDisplay::fill_rect(DisplayRect area, std::uint16_t color)
{
    if (!started_)
        return Status::not_ready;
    if (!inside_(area))
        return Status::invalid_argument;
    std::uint16_t* row = config_.frame_buffers[shown_] + area.y * config_.width + area.x;
    for (std::int32_t line = 0; line < area.height; ++line, row += config_.width)
        std::fill_n(row, area.width, color);
    return Status::ok;
}

/// After present_frame_buffer(): until the new buffer is on the screen (the
/// LTDC clears the reload request at the vertical blanking), which may
/// already be the case: then it returns at once. Otherwise until the next
/// vertical blanking starts.
Status LtdcDisplay::wait_for_vsync(std::uint32_t timeout_ms)
{
    if (!started_)
        return Status::not_ready;
    const std::uint32_t start = HAL_GetTick();
    if (flip_pending_)
    {
        while ((LTDC->SRCR & LTDC_SRCR_VBR) != 0)
            if (HAL_GetTick() - start > timeout_ms)
                return Status::timeout;
        flip_pending_ = false;
        return Status::ok;
    }
    while ((LTDC->CDSR & LTDC_CDSR_VSYNCS) != 0)
        if (HAL_GetTick() - start > timeout_ms)
            return Status::timeout;
    while ((LTDC->CDSR & LTDC_CDSR_VSYNCS) == 0)
        if (HAL_GetTick() - start > timeout_ms)
            return Status::timeout;
    return Status::ok;
}

std::uint8_t LtdcDisplay::frame_buffer_count() const
{
    return config_.frame_buffer_count;
}

Status LtdcDisplay::get_frame_buffer(std::uint8_t index, brick::interfaces::display::WritablePixelBuffer& buffer)
{
    if (!started_)
        return Status::not_ready;
    if (index >= config_.frame_buffer_count)
        return Status::invalid_argument;
    buffer = {
        reinterpret_cast<std::uint8_t*>(config_.frame_buffers[index]), config_.width,  config_.height,
        static_cast<std::size_t>(config_.width) * kPixelBytes,         pixel_format(), true,
    };
    return Status::ok;
}

Status LtdcDisplay::present_frame_buffer(std::uint8_t index)
{
    if (!started_)
        return Status::not_ready;
    if (index >= config_.frame_buffer_count)
        return Status::invalid_argument;
    __DSB();  // the last pixel writes reach the memory before the LTDC reads it
    LTDC_Layer1->CFBAR = reinterpret_cast<std::uint32_t>(config_.frame_buffers[index]);
    LTDC->SRCR         = LTDC_SRCR_VBR;
    shown_             = index;
    flip_pending_      = true;
    return Status::ok;
}

Status LtdcDisplay::set_vsync_front_porch(std::uint16_t lines)
{
    if (!started_)
        return Status::not_ready;
    const std::uint32_t total = config_.timing.vsync_width + config_.timing.vsync_back_porch + config_.height + lines;
    if (lines == 0 || total - 1U > LTDC_TWCR_TOTALH)
        return Status::invalid_argument;
    // The total is not shadowed: written while the picture's lines go out,
    // both the old end of the frame and the new one are still ahead.
    const std::uint32_t start = HAL_GetTick();
    while ((LTDC->CDSR & LTDC_CDSR_VDES) == 0U)
        if (HAL_GetTick() - start > kBlankingTimeoutMs)
            return Status::timeout;
    LTDC->TWCR                       = (LTDC->TWCR & ~LTDC_TWCR_TOTALH) | (total - 1U);
    config_.timing.vsync_front_porch = lines;
    return Status::ok;
}

Status LtdcDisplay::copy_to_frame_buffer(std::uint8_t index, DisplayRect area, const PixelBuffer& pixels)
{
    if (!started_)
        return Status::not_ready;
    if (index >= config_.frame_buffer_count || !inside_(area) || !pixels.valid() || pixels.format != PixelFormat::rgb565
        || pixels.width != static_cast<std::uint32_t>(area.width)
        || pixels.height != static_cast<std::uint32_t>(area.height)
        || pixels.stride_bytes < static_cast<std::size_t>(area.width) * kPixelBytes)
        return Status::invalid_argument;
    return copy_rows_(index, area, pixels);
}

Status LtdcDisplay::copy_between_frame_buffers(std::uint8_t from, std::uint8_t to, DisplayRect area)
{
    if (!started_)
        return Status::not_ready;
    if (from >= config_.frame_buffer_count || to >= config_.frame_buffer_count || !inside_(area))
        return Status::invalid_argument;
    if (from == to)
        return Status::ok;
    const std::size_t   stride = static_cast<std::size_t>(config_.width) * kPixelBytes;
    const std::uint8_t* source =
        reinterpret_cast<const std::uint8_t*>(config_.frame_buffers[from]) + area.y * stride + area.x * kPixelBytes;
    const PixelBuffer pixels{ source,
                              static_cast<std::uint32_t>(area.width),
                              static_cast<std::uint32_t>(area.height),
                              stride,
                              PixelFormat::rgb565,
                              true };
    return copy_rows_(to, area, pixels);
}

void LtdcDisplay::set_backlight(bool on)
{
    write(config_.backlight_enable, on == config_.backlight_active_high);
}

bool LtdcDisplay::inside_(DisplayRect area) const
{
    return !area.empty() && DisplayRect{ 0, 0, config_.width, config_.height }.contains(area);
}

Status LtdcDisplay::copy_rows_(std::uint8_t index, DisplayRect area, const PixelBuffer& pixels)
{
    const std::size_t stride = static_cast<std::size_t>(config_.width) * kPixelBytes;
    const std::size_t bytes  = static_cast<std::size_t>(area.width) * kPixelBytes;
    std::uint8_t*     target =
        reinterpret_cast<std::uint8_t*>(config_.frame_buffers[index]) + area.y * stride + area.x * kPixelBytes;
    const std::uint8_t* source = pixels.data;
    for (std::int32_t line = 0; line < area.height; ++line, target += stride, source += pixels.stride_bytes)
        std::memcpy(target, source, bytes);
    return Status::ok;
}

}  // namespace brick::platform::stm32::h7
