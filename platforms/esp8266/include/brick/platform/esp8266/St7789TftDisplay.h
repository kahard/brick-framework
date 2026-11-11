#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"

namespace brick::platform::esp8266
{

struct St7789TftDisplayConfig
{
    std::uint16_t width              = 240;
    std::uint16_t height             = 240;
    std::uint8_t  rotation           = 0;
    std::uint8_t  backlight_gpio     = 5;
    std::uint8_t  backlight_on_level = LOW;
    bool          enable_backlight   = true;
    bool          swap_bytes         = true;
};

class St7789TftDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit St7789TftDisplay(St7789TftDisplayConfig config);
    Status                                          begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    Status set_rotation(brick::interfaces::display::Rotation rotation) override;
    /// Sends the pixels before it returns, at the SPI clock rate.
    Status draw_buffer(brick::interfaces::display::DisplayRect        area,
                       const brick::interfaces::display::PixelBuffer& buffer) override;
    /// Returns once the first 64 bytes go out; the HSPI interrupt sends the
    /// rest (async_transfer) while the caller prepares the next buffer. A
    /// buffer outside DRAM (flash) or not word-aligned is sent at once, as by
    /// draw_buffer().
    Status submit_buffer(brick::interfaces::display::DisplayRect        area,
                         const brick::interfaces::display::PixelBuffer& buffer) override;
    Status wait_for_transfer_complete(std::uint32_t timeout_ms) override;
    /// TFT_eSPI's fillRect(): the color goes into the SPI FIFO 32 bits at a
    /// time, without a pixel buffer or a byte swap per pixel.
    Status fill_rect(brick::interfaces::display::DisplayRect area, std::uint16_t color) override;

private:
    /// 240 x 240 pixels take 23 ms at 40 MHz.
    static constexpr std::uint32_t kTransferTimeoutMs = 1000;

    Status check_(brick::interfaces::display::DisplayRect        area,
                  const brick::interfaces::display::PixelBuffer& buffer) const;
    /// Waits for a submitted transfer before the next SPI operation.
    Status finish_transfer_();
    void   end_transfer_();
    bool   inside_(brick::interfaces::display::DisplayRect area) const;

    St7789TftDisplayConfig config_;
    TFT_eSPI               tft_;
    bool                   initialized_  = false;
    bool                   transferring_ = false;
};

}  // namespace brick::platform::esp8266
