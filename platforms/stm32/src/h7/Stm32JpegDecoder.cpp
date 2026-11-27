#include "brick/platform/stm32/h7/Stm32JpegDecoder.h"

#include <algorithm>

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

namespace
{
    // Bytes of one MCU in the codec's output: 8x8 blocks of Y, Cb and Cr.
    constexpr std::size_t   kBlockBytes = 64;
    constexpr std::uint32_t kPixelBytes = 2;

    /// The decoder the HAL callbacks report to while decode() runs.
    Stm32JpegDecoder* g_active = nullptr;

    std::uint32_t mcu_width(std::uint32_t subsampling)
    {
        return subsampling == JPEG_444_SUBSAMPLING ? 8U : 16U;
    }

    std::size_t mcu_bytes(std::uint32_t subsampling)
    {
        if (subsampling == JPEG_420_SUBSAMPLING)
            return 6 * kBlockBytes;  // 4 Y + Cb + Cr
        if (subsampling == JPEG_422_SUBSAMPLING)
            return 4 * kBlockBytes;  // 2 Y + Cb + Cr
        return 3 * kBlockBytes;
    }

    std::uint32_t dma2d_subsampling(std::uint32_t subsampling)
    {
        if (subsampling == JPEG_420_SUBSAMPLING)
            return DMA2D_CSS_420;
        if (subsampling == JPEG_422_SUBSAMPLING)
            return DMA2D_CSS_422;
        return DMA2D_NO_CSS;
    }
}  // namespace

Status Stm32JpegDecoder::begin()
{
    if (started_)
        return Status::ok;
    if (config_.ycbcr == nullptr || config_.ycbcr_capacity == 0)
        return Status::invalid_argument;
    __HAL_RCC_JPGDECEN_CLK_ENABLE();
    __HAL_RCC_DMA2D_CLK_ENABLE();
    jpeg_.Instance = JPEG;
    if (const HAL_StatusTypeDef result = HAL_JPEG_Init(&jpeg_); result != HAL_OK)
        return to_status(result);
    dma2d_.Instance = DMA2D;
    started_        = true;
    return Status::ok;
}

Status Stm32JpegDecoder::decode(const std::uint8_t* jpeg, std::size_t size, JpegImage& image)
{
    banded_ = false;
    if (const Status status = start_(jpeg, size); !is_ok(status))
        return status;
    if (const Status status = finish_(); !is_ok(status))
        return status;
    const std::size_t mcu_rows = (image_.height + mcu_height_() - 1U) / mcu_height_();
    if (output_bytes_ < mcu_rows * mcu_row_bytes_())
        return Status::no_memory;  // the YCbCr buffer is too small for this image
    image = image_;
    return Status::ok;
}

Status Stm32JpegDecoder::decode_to_rgb565(const std::uint8_t* jpeg, std::size_t size,
                                          const brick::interfaces::display::WritablePixelBuffer& target, std::int32_t x,
                                          std::int32_t y, JpegImage& image)
{
    if (!target.valid() || target.format != brick::interfaces::display::PixelFormat::rgb565 || x < 0 || y < 0)
        return Status::invalid_argument;
    banded_ = true;
    band_   = { &target, x, y, 0, Status::ok };
    if (const Status status = start_(jpeg, size); !is_ok(status))
        return status;
    if (const Status status = finish_(); !is_ok(status))
        return status;
    if (!is_ok(band_.status))
        return band_.status;
    if (band_.row < image_.height)
        return Status::error;  // the codec gave fewer rows than the image has
    image = image_;
    return Status::ok;
}

Status Stm32JpegDecoder::to_rgb565(const brick::interfaces::display::WritablePixelBuffer& target, std::int32_t x,
                                   std::int32_t y, std::uint16_t first_row, std::uint16_t rows)
{
    if (!started_)
        return Status::not_ready;
    if (image_.width == 0 || first_row >= image_.height || first_row % mcu_height_() != 0)
        return Status::invalid_argument;
    if (rows == 0 || first_row + rows > image_.height)
        rows = static_cast<std::uint16_t>(image_.height - first_row);
    return convert_(config_.ycbcr + (first_row / mcu_height_()) * mcu_row_bytes_(), target, x, y, rows);
}

/// Runs the codec over the whole JPEG; the callbacks fill in the image and,
/// in band mode, convert the bands.
Status Stm32JpegDecoder::start_(const std::uint8_t* jpeg, std::size_t size)
{
    if (!started_)
        return Status::not_ready;
    if (jpeg == nullptr || size == 0)
        return Status::invalid_argument;
    if (g_active != nullptr)
        return Status::busy;
    g_active         = this;
    decoded_         = false;
    failed_          = false;
    output_bytes_    = 0;
    color_space_     = JPEG_GRAYSCALE_COLORSPACE;
    image_           = {};
    const auto words = static_cast<std::uint32_t>((size + 3U) & ~static_cast<std::size_t>(3U));
    // HAL_JPEG_Decode takes a non-const input buffer but only reads it.
    const HAL_StatusTypeDef result =
        HAL_JPEG_Decode(&jpeg_, const_cast<std::uint8_t*>(jpeg), words, config_.ycbcr,
                        static_cast<std::uint32_t>(config_.ycbcr_capacity), config_.timeout_ms);
    g_active = nullptr;
    return to_status(result);
}

Status Stm32JpegDecoder::finish_()
{
    if (failed_ || !decoded_ || image_.width == 0 || image_.height == 0)
        return Status::error;
    if (color_space_ != JPEG_YCBCR_COLORSPACE)
        return Status::not_supported;
    return Status::ok;
}

/// rows rows of the image from source (whole MCU rows) to RGB565 at (x, y).
Status Stm32JpegDecoder::convert_(const std::uint8_t*                                    source,
                                  const brick::interfaces::display::WritablePixelBuffer& target, std::int32_t x,
                                  std::int32_t y, std::uint16_t rows)
{
    const auto stride_pixels = static_cast<std::int32_t>(target.stride_bytes / kPixelBytes);
    if (!target.valid() || target.format != brick::interfaces::display::PixelFormat::rgb565 || x < 0 || y < 0
        || x + image_.width > static_cast<std::int32_t>(target.width)
        || y + rows > static_cast<std::int32_t>(target.height))
        return Status::invalid_argument;

    const std::uint32_t width_in_mcu = mcu_width(image_.subsampling);
    const std::uint32_t tail         = image_.width % width_in_mcu;
    dma2d_.Init.Mode                 = DMA2D_M2M_PFC;
    dma2d_.Init.ColorMode            = DMA2D_OUTPUT_RGB565;
    dma2d_.Init.OutputOffset         = static_cast<std::uint32_t>(stride_pixels - image_.width);
    dma2d_.Init.AlphaInverted        = DMA2D_REGULAR_ALPHA;
    dma2d_.Init.RedBlueSwap          = DMA2D_RB_REGULAR;
    dma2d_.Init.BytesSwap            = DMA2D_BYTES_REGULAR;
    dma2d_.Init.LineOffsetMode       = DMA2D_LOM_PIXELS;
    DMA2D_LayerCfgTypeDef& layer     = dma2d_.LayerCfg[1];
    layer.InputOffset                = tail == 0 ? 0U : width_in_mcu - tail;
    layer.InputColorMode             = DMA2D_INPUT_YCBCR;
    layer.ChromaSubSampling          = dma2d_subsampling(image_.subsampling);
    layer.AlphaMode                  = DMA2D_REPLACE_ALPHA;
    layer.InputAlpha                 = 0xFF;
    layer.AlphaInverted              = DMA2D_REGULAR_ALPHA;
    layer.RedBlueSwap                = DMA2D_RB_REGULAR;
    if (const HAL_StatusTypeDef result = HAL_DMA2D_Init(&dma2d_); result != HAL_OK)
        return to_status(result);
    if (const HAL_StatusTypeDef result = HAL_DMA2D_ConfigLayer(&dma2d_, 1); result != HAL_OK)
        return to_status(result);
    std::uint8_t* output = target.data + y * target.stride_bytes + x * kPixelBytes;
    if (const HAL_StatusTypeDef result = HAL_DMA2D_Start(&dma2d_, reinterpret_cast<std::uint32_t>(source),
                                                         reinterpret_cast<std::uint32_t>(output), image_.width, rows);
        result != HAL_OK)
        return to_status(result);
    return to_status(HAL_DMA2D_PollForTransfer(&dma2d_, config_.timeout_ms));
}

void Stm32JpegDecoder::on_info(const JPEG_ConfTypeDef& info)
{
    color_space_       = info.ColorSpace;
    image_.width       = static_cast<std::uint16_t>(info.ImageWidth);
    image_.height      = static_cast<std::uint16_t>(info.ImageHeight);
    image_.subsampling = info.ChromaSubsampling;
    if (!banded_)
        return;
    // Bands of whole MCU rows, as many as the buffer holds.
    const std::size_t row_bytes  = mcu_row_bytes_();
    const std::size_t band_bytes = row_bytes == 0 ? 0 : config_.ycbcr_capacity / row_bytes * row_bytes;
    if (band_bytes == 0 || band_.x + image_.width > static_cast<std::int32_t>(band_.buffer->width)
        || band_.y + image_.height > static_cast<std::int32_t>(band_.buffer->height))
    {
        band_.status = band_bytes == 0 ? Status::no_memory : Status::invalid_argument;
        return;
    }
    HAL_JPEG_ConfigOutputBuffer(&jpeg_, config_.ycbcr, static_cast<std::uint32_t>(band_bytes));
}

void Stm32JpegDecoder::on_input_used()
{
    // The whole JPEG was handed over at once: no more input.
    HAL_JPEG_ConfigInputBuffer(&jpeg_, jpeg_.pJpegInBuffPtr, 0);
}

/// The codec filled the buffer (or ended): in band mode the band goes to the
/// target now, and the codec goes on into the same buffer.
void Stm32JpegDecoder::on_output(std::uint8_t* data, std::uint32_t bytes)
{
    output_bytes_ += bytes;
    if (!banded_ || !is_ok(band_.status) || band_.row >= image_.height)
        return;
    const std::size_t   row_bytes = mcu_row_bytes_();
    const auto          mcu_rows  = static_cast<std::uint32_t>((bytes + row_bytes - 1U) / row_bytes);
    const std::uint32_t rows      = std::min<std::uint32_t>(mcu_rows * mcu_height_(), image_.height - band_.row);
    SCB_CleanDCache_by_Addr(reinterpret_cast<std::uint32_t*>(data), static_cast<std::int32_t>(bytes));
    band_.status = convert_(data, *band_.buffer, band_.x, band_.y + band_.row, static_cast<std::uint16_t>(rows));
    band_.row    = static_cast<std::uint16_t>(band_.row + rows);
}

void Stm32JpegDecoder::on_done(bool error)
{
    failed_  = failed_ || error;
    decoded_ = true;
}

std::uint32_t Stm32JpegDecoder::mcu_height_() const
{
    return image_.subsampling == JPEG_420_SUBSAMPLING ? 16U : 8U;
}

std::size_t Stm32JpegDecoder::mcu_row_bytes_() const
{
    const std::uint32_t width = mcu_width(image_.subsampling);
    return ((image_.width + width - 1U) / width) * mcu_bytes(image_.subsampling);
}

}  // namespace brick::platform::stm32::h7

// The HAL's JPEG callbacks, passed on to the decoder that is decoding.
extern "C" void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef* handle, JPEG_ConfTypeDef* info)
{
    (void)handle;
    if (brick::platform::stm32::h7::g_active != nullptr && info != nullptr)
        brick::platform::stm32::h7::g_active->on_info(*info);
}

extern "C" void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef* handle, uint32_t decoded)
{
    (void)handle;
    (void)decoded;
    if (brick::platform::stm32::h7::g_active != nullptr)
        brick::platform::stm32::h7::g_active->on_input_used();
}

extern "C" void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef* handle, uint8_t* data, uint32_t bytes)
{
    (void)handle;
    if (brick::platform::stm32::h7::g_active != nullptr)
        brick::platform::stm32::h7::g_active->on_output(data, bytes);
}

extern "C" void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef* handle)
{
    (void)handle;
    if (brick::platform::stm32::h7::g_active != nullptr)
        brick::platform::stm32::h7::g_active->on_done(false);
}

extern "C" void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef* handle)
{
    (void)handle;
    if (brick::platform::stm32::h7::g_active != nullptr)
        brick::platform::stm32::h7::g_active->on_done(true);
}
