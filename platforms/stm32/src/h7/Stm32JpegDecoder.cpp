#include "brick/platform/stm32/h7/Stm32JpegDecoder.h"

#include <algorithm>
#include <cstring>

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

namespace
{
    // Bytes of one MCU in the codec's output: 8x8 blocks of Y, Cb and Cr.
    constexpr std::size_t   kBlockBytes = 64;
    constexpr std::uint32_t kPixelBytes = 2;
    constexpr std::size_t   kWordBytes  = sizeof(std::uint32_t);
    /// Words a FIFO of the codec takes or gives at once when it flags its threshold.
    constexpr std::size_t kFifoBurst = 8;
    /// Rounds of the codec loop between two looks at the clock.
    constexpr std::uint32_t kClockRounds = 1024;
    constexpr std::uint32_t kInterrupts =
        JPEG_CR_IFTIE | JPEG_CR_IFNFIE | JPEG_CR_OFTIE | JPEG_CR_OFNEIE | JPEG_CR_EOCIE | JPEG_CR_HPDIE;
    constexpr std::uint32_t kClearFlags = JPEG_CFR_CEOCF | JPEG_CFR_CHPDF;

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
    if (config_.ycbcr == nullptr || config_.ycbcr_capacity == 0
        || reinterpret_cast<std::uintptr_t>(config_.ycbcr) % kWordBytes != 0)
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
    if (const Status status = run_(jpeg, size); !is_ok(status))
        return status;
    if (output_bytes_ < band_bytes_)
        return Status::error;  // the codec gave less than the image holds
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
    band_   = { &target, x, y, 0 };
    if (const Status status = run_(jpeg, size); !is_ok(status))
        return status;
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
    if (const Status status =
            convert_(config_.ycbcr + (first_row / mcu_height_()) * mcu_row_bytes_(), target, x, y, rows);
        !is_ok(status))
        return status;
    return wait_converted_();
}

/// Runs the codec over the whole JPEG: its input FIFO fed from the data, its
/// output FIFO emptied into the YCbCr buffer, both by words as the FIFOs
/// flag room and data. The header, once the codec has parsed it, says where
/// the output goes (header_parsed_()).
Status Stm32JpegDecoder::run_(const std::uint8_t* jpeg, std::size_t size)
{
    if (!started_)
        return Status::not_ready;
    if (jpeg == nullptr || size == 0)
        return Status::invalid_argument;
    image_        = {};
    output_       = nullptr;
    output_from_  = nullptr;
    output_end_   = nullptr;
    band_bytes_   = 0;
    output_bytes_ = 0;

    JPEG_TypeDef& codec = *jpeg_.Instance;
    codec.CONFR0 &= ~JPEG_CONFR0_START;
    codec.CONFR1 |= JPEG_CONFR1_DE | JPEG_CONFR1_HDR;
    codec.CR &= ~kInterrupts;
    codec.CR |= JPEG_CR_IFF;
    codec.CR |= JPEG_CR_OFF;
    codec.CFR = kClearFlags;
    codec.CONFR0 |= JPEG_CONFR0_START;

    const std::uint8_t*       input     = jpeg;
    const std::uint8_t* const input_end = jpeg + ((size + kWordBytes - 1U) & ~(kWordBytes - 1U));
    const std::uint32_t       from      = HAL_GetTick();
    std::uint32_t             rounds    = 0;
    Status                    status    = Status::ok;
    while (is_ok(status))
    {
        const std::uint32_t flags = codec.SR;
        if ((flags & JPEG_SR_HPDF) != 0U)
        {
            codec.CFR = JPEG_CFR_CHPDF;
            status    = header_parsed_();
            continue;
        }
        if (input != input_end && (flags & (JPEG_SR_IFTF | JPEG_SR_IFNFF)) != 0U)
        {
            const std::size_t room = (flags & JPEG_SR_IFTF) != 0U ? kFifoBurst : 1U;
            const auto        left = static_cast<std::size_t>(input_end - input) / kWordBytes;
            for (std::size_t words = std::min(room, left); words != 0; --words)
            {
                std::uint32_t word = 0;
                std::memcpy(&word, input, kWordBytes);  // the JPEG need not be word-aligned
                codec.DIR = word;
                input += kWordBytes;
            }
        }
        if ((flags & (JPEG_SR_OFTF | JPEG_SR_OFNEF)) != 0U)
        {
            if (output_ == output_end_)
            {
                status = Status::error;  // data without a header, or more than the image holds
                break;
            }
            const std::size_t ready = (flags & JPEG_SR_OFTF) != 0U ? kFifoBurst : 1U;
            const auto        left  = static_cast<std::size_t>(output_end_ - output_);
            for (std::size_t words = std::min(ready, left); words != 0; --words)
                *output_++ = codec.DOR;
            if (output_ == output_end_)
                status = output_filled_();
        }
        else if ((flags & JPEG_SR_EOCF) != 0U)
        {
            break;  // the end of the image, and nothing left in the FIFO
        }
        if (++rounds % kClockRounds == 0U && HAL_GetTick() - from > config_.timeout_ms)
            status = Status::timeout;
    }
    codec.CONFR0 &= ~JPEG_CONFR0_START;
    codec.CFR = kClearFlags;
    if (!is_ok(status))
    {
        // What the codec still holds of this image must not reach the next one.
        codec.CR |= JPEG_CR_IFF;
        codec.CR |= JPEG_CR_OFF;
    }
    if (is_ok(status) && output_from_ == nullptr)
        status = Status::error;  // the codec found no image in the data
    if (is_ok(status) && banded_ && output_ != output_from_)
        status = band_decoded_();  // the image's last band
    if (!banded_)
        output_bytes_ = static_cast<std::uint32_t>((output_ - output_from_) * kWordBytes);
    if (const Status converted = wait_converted_(); is_ok(status))
        status = converted;
    return status;
}

/// The codec has read the image's header: its size and layout are known, and
/// with them where the output goes: the whole image (decode()) or bands of
/// whole MCU rows, two of them when the buffer holds two.
Status Stm32JpegDecoder::header_parsed_()
{
    JPEG_ConfTypeDef info{};
    if (const HAL_StatusTypeDef result = HAL_JPEG_GetInfo(&jpeg_, &info); result != HAL_OK)
        return to_status(result);
    image_.width       = static_cast<std::uint16_t>(info.ImageWidth);
    image_.height      = static_cast<std::uint16_t>(info.ImageHeight);
    image_.subsampling = info.ChromaSubsampling;
    if (image_.width == 0 || image_.height == 0)
        return Status::error;
    if (info.ColorSpace != JPEG_YCBCR_COLORSPACE)
        return Status::not_supported;
    const std::size_t row_bytes = mcu_row_bytes_();
    if (banded_)
    {
        if (band_.x + image_.width > static_cast<std::int32_t>(band_.buffer->width)
            || band_.y + image_.height > static_cast<std::int32_t>(band_.buffer->height))
            return Status::invalid_argument;
        const std::size_t half = config_.ycbcr_capacity / 2U / row_bytes * row_bytes;
        two_bands_             = half != 0;
        band_bytes_            = two_bands_ ? half : config_.ycbcr_capacity / row_bytes * row_bytes;
    }
    else
    {
        band_bytes_ = (image_.height + mcu_height_() - 1U) / mcu_height_() * row_bytes;
    }
    if (band_bytes_ == 0 || band_bytes_ > config_.ycbcr_capacity)
        return Status::no_memory;
    output_from_ = reinterpret_cast<std::uint32_t*>(config_.ycbcr);
    output_      = output_from_;
    output_end_  = output_from_ + band_bytes_ / kWordBytes;
    return Status::ok;
}

/// The codec filled what it was given: a band, which the DMA2D takes over
/// while the codec goes on (the whole image of decode() is simply full).
Status Stm32JpegDecoder::output_filled_()
{
    if (!banded_)
        return Status::ok;
    if (const Status status = band_decoded_(); !is_ok(status))
        return status;
    if (config_.observer != nullptr)
        config_.observer->band_decoded();
    return Status::ok;
}

/// The band at output_from_ goes to the target: the DMA2D converts it while
/// the codec fills the buffer's other half, where the band before has been
/// converted by now (or is waited for).
Status Stm32JpegDecoder::band_decoded_()
{
    auto* const data  = reinterpret_cast<std::uint8_t*>(output_from_);
    const auto  bytes = static_cast<std::uint32_t>((output_ - output_from_) * kWordBytes);
    output_bytes_ += bytes;
    if (band_.row < image_.height)
    {
        const std::size_t   row_bytes = mcu_row_bytes_();
        const auto          mcu_rows  = static_cast<std::uint32_t>((bytes + row_bytes - 1U) / row_bytes);
        const std::uint32_t whole     = mcu_rows * mcu_height_();
        const std::uint32_t rows      = std::min<std::uint32_t>(whole, image_.height - band_.row);
        // The DMA2D converts YCbCr only in whole MCU rows (a picture 450 rows
        // high ends in a part of one): convert the whole last row when the
        // target has room below the picture, else only the picture's rows.
        const auto top       = static_cast<std::uint32_t>(band_.y + band_.row);
        const auto room      = static_cast<std::uint32_t>(band_.buffer->height) - top;
        const auto converted = static_cast<std::uint16_t>(whole <= room ? whole : rows);
        if (const Status status = wait_converted_(); !is_ok(status))
            return status;
        SCB_CleanDCache_by_Addr(reinterpret_cast<std::uint32_t*>(data), static_cast<std::int32_t>(bytes));
        if (const Status status = convert_(data, *band_.buffer, band_.x, band_.y + band_.row, converted);
            !is_ok(status))
            return status;
        band_.row = static_cast<std::uint16_t>(band_.row + rows);
    }
    if (two_bands_)
    {
        auto* const first = reinterpret_cast<std::uint32_t*>(config_.ycbcr);
        output_from_      = output_from_ == first ? first + band_bytes_ / kWordBytes : first;
    }
    else if (const Status status = wait_converted_(); !is_ok(status))
    {
        return status;  // the one band is in use until it is converted
    }
    output_     = output_from_;
    output_end_ = output_from_ + band_bytes_ / kWordBytes;
    return Status::ok;
}

/// Starts the DMA2D on rows rows of the image from source (whole MCU rows)
/// to RGB565 at (x, y); wait_converted_() ends it.
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
    converting_ = true;
    return Status::ok;
}

/// Waits for the conversion convert_() started, if one runs.
Status Stm32JpegDecoder::wait_converted_()
{
    if (!converting_)
        return Status::ok;
    converting_ = false;
    return to_status(HAL_DMA2D_PollForTransfer(&dma2d_, config_.timeout_ms));
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
