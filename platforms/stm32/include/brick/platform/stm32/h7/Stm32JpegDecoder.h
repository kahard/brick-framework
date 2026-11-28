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

/// Told each time decode_to_rgb565() has a band behind it and goes on with
/// the next: a few times per image, for work that cannot wait for a whole one
/// (the acknowledgements of a serial link, say). The codec stands still
/// meanwhile, so it should be short.
class IJpegBandObserver
{
public:
    virtual ~IJpegBandObserver() = default;
    virtual void band_decoded()  = 0;
};

struct Stm32JpegDecoderConfig
{
    /// Where the codec's decoded MCUs go, word-aligned. For decode() the
    /// whole image: memory the CPU writes through to (the SDRAM,
    /// write-through), so the DMA2D reads it without a cache clean; 3 bytes
    /// per pixel take any image (4:4:4), 4:2:0 needs 1.5. For
    /// decode_to_rgb565() two bands of it: any memory the DMA2D reads.
    std::uint8_t*      ycbcr          = nullptr;
    std::size_t        ycbcr_capacity = 0;
    std::uint32_t      timeout_ms     = 200;
    IJpegBandObserver* observer       = nullptr;
};

/// Baseline JPEG decoding with the STM32H7 hardware codec, then YCbCr to
/// RGB565 with the DMA2D straight into a frame buffer. The CPU moves the
/// codec's data, a word at a time between its FIFOs and memory, and waits for
/// the whole image; to_rgb565() converts it, or a band of whole MCU rows of
/// it, and waits for the DMA2D. Colour (YCbCr) images only; grey ones give
/// not_supported.
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

    /// Decodes and converts in one go, in bands of whole MCU rows: the codec
    /// fills one half of the YCbCr buffer while the DMA2D turns the other,
    /// the band before, into RGB565 at (x, y) of target. So the buffer may be
    /// much smaller than the image and sit in fast on-chip RAM (one too small
    /// for two bands takes one, and the codec waits for each conversion); it
    /// is cleaned from the data cache before each conversion. The image must
    /// fit target.
    Status decode_to_rgb565(const std::uint8_t* jpeg, std::size_t size,
                            const brick::interfaces::display::WritablePixelBuffer& target, std::int32_t x,
                            std::int32_t y, JpegImage& image);

private:
    Status        run_(const std::uint8_t* jpeg, std::size_t size);
    Status        header_parsed_();
    Status        output_filled_();
    Status        band_decoded_();
    Status        convert_(const std::uint8_t* source, const brick::interfaces::display::WritablePixelBuffer& target,
                           std::int32_t x, std::int32_t y, std::uint16_t rows);
    Status        wait_converted_();
    std::uint32_t mcu_height_() const;
    std::size_t   mcu_row_bytes_() const;

    /// Where decode_to_rgb565() puts the bands while it runs.
    struct BandTarget
    {
        const brick::interfaces::display::WritablePixelBuffer* buffer = nullptr;
        std::int32_t                                           x      = 0;
        std::int32_t                                           y      = 0;
        std::uint16_t                                          row    = 0;  ///< next image row to convert
    };

    Stm32JpegDecoderConfig config_;
    JPEG_HandleTypeDef     jpeg_{};
    DMA2D_HandleTypeDef    dma2d_{};
    JpegImage              image_{};
    BandTarget             band_{};
    std::uint32_t*         output_       = nullptr;  ///< where the codec's next word goes
    std::uint32_t*         output_from_  = nullptr;  ///< start of the band (or image) being filled
    std::uint32_t*         output_end_   = nullptr;
    std::size_t            band_bytes_   = 0;
    std::uint32_t          output_bytes_ = 0;
    bool                   two_bands_    = false;
    bool                   converting_   = false;  ///< the DMA2D runs a conversion started here
    bool                   banded_       = false;
    bool                   started_      = false;
};

}  // namespace brick::platform::stm32::h7
