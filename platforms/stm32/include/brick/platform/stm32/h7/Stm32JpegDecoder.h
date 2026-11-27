#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/display/WritablePixelBuffer.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

/// A decoded image in the codec's YCbCr MCU layout.
struct JpegImage
{
    std::uint16_t width       = 0;
    std::uint16_t height      = 0;
    std::uint32_t subsampling = JPEG_420_SUBSAMPLING;  ///< JPEG_444/422/420_SUBSAMPLING
};

struct Stm32JpegDecoderConfig
{
    /// Where the codec writes the decoded MCUs: memory the CPU writes
    /// through to (the SDRAM, write-through) so the DMA2D reads them without
    /// a cache clean. 3 bytes per pixel take any image (4:4:4); 4:2:0 needs
    /// 1.5.
    std::uint8_t* ycbcr          = nullptr;
    std::size_t   ycbcr_capacity = 0;
    std::uint32_t timeout_ms     = 200;
};

/// Baseline JPEG decoding with the STM32H7 hardware codec, then YCbCr to
/// RGB565 with the DMA2D straight into a frame buffer. decode() feeds the
/// codec by CPU (HAL polling mode) and waits for the whole image;
/// to_rgb565() converts it, or a band of whole MCU rows of it, and waits for
/// the DMA2D. Colour (YCbCr) images only; grey ones give not_supported.
///
/// The HAL reports to C callbacks without a context, so one decoder at a
/// time runs; a second decode() while one runs gives Status::busy.
class Stm32JpegDecoder final
{
public:
    explicit Stm32JpegDecoder(Stm32JpegDecoderConfig config) : config_(config) {}

    Status begin();

    /// Decodes a JPEG of size bytes. The codec reads 32-bit words, so the
    /// data must be readable up to size rounded up to 4 bytes (what follows
    /// the end marker is ignored).
    Status decode(const std::uint8_t* jpeg, std::size_t size, JpegImage& image);

    /// Converts rows [first_row, first_row + rows) of the image last decoded
    /// to RGB565 at (x, y) of target. first_row and rows are multiples of the
    /// MCU height (16 for 4:2:0, 8 otherwise) except for the image's last row;
    /// rows 0 means to the end of the image.
    Status to_rgb565(const brick::interfaces::display::WritablePixelBuffer& target, std::int32_t x, std::int32_t y,
                     std::uint16_t first_row = 0, std::uint16_t rows = 0);

    /// Decodes and converts in one go, in bands: the codec fills the YCbCr
    /// buffer with whole MCU rows, the DMA2D turns each band into RGB565 at
    /// (x, y) of target while the codec waits, and the buffer is used again.
    /// So the buffer may be much smaller than the image (it is cut down to
    /// whole MCU rows) and sit in fast on-chip RAM; it is cleaned from the
    /// data cache before each conversion. The image must fit target.
    Status decode_to_rgb565(const std::uint8_t* jpeg, std::size_t size,
                            const brick::interfaces::display::WritablePixelBuffer& target, std::int32_t x,
                            std::int32_t y, JpegImage& image);

    /// Called by the HAL callbacks (Stm32JpegDecoder.cpp).
    void on_info(const JPEG_ConfTypeDef& info);
    void on_input_used();
    void on_output(std::uint8_t* data, std::uint32_t bytes);
    void on_done(bool error);

private:
    Status        start_(const std::uint8_t* jpeg, std::size_t size);
    Status        finish_();
    Status        convert_(const std::uint8_t* source, const brick::interfaces::display::WritablePixelBuffer& target,
                           std::int32_t x, std::int32_t y, std::uint16_t rows);
    std::uint32_t mcu_height_() const;
    std::size_t   mcu_row_bytes_() const;

    /// Where decode_to_rgb565() puts the bands while it runs.
    struct BandTarget
    {
        const brick::interfaces::display::WritablePixelBuffer* buffer = nullptr;
        std::int32_t                                           x      = 0;
        std::int32_t                                           y      = 0;
        std::uint16_t                                          row    = 0;  ///< next image row to convert
        Status                                                 status = Status::ok;
    };

    Stm32JpegDecoderConfig config_;
    JPEG_HandleTypeDef     jpeg_{};
    DMA2D_HandleTypeDef    dma2d_{};
    JpegImage              image_{};
    BandTarget             band_{};
    std::uint32_t          color_space_  = 0;
    std::uint32_t          output_bytes_ = 0;
    bool                   decoded_      = false;
    bool                   failed_       = false;
    bool                   banded_       = false;
    bool                   started_      = false;
};

}  // namespace brick::platform::stm32::h7
