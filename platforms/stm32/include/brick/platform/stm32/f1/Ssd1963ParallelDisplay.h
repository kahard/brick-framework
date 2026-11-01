#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/platform/stm32/f1/Gpio.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

/// Panel timing as written to the SSD1963 (commands 0xB4, 0xB6 and 0xE6),
/// from the panel datasheet, in pixel clocks and lines.
struct Ssd1963PanelTiming
{
    std::uint16_t horizontal_total      = 0;  ///< HT
    std::uint16_t horizontal_sync_start = 0;  ///< HPS
    std::uint8_t  horizontal_sync_width = 0;  ///< HPW
    std::uint16_t vertical_total        = 0;  ///< VT
    std::uint16_t vertical_sync_start   = 0;  ///< VPS
    std::uint8_t  vertical_sync_width   = 0;  ///< VPW
    /// LCDC_FPR, 20 bits: pixel clock = PLL clock * (value + 1) / 2^20.
    std::uint32_t pixel_clock_ratio = 0;
};

struct Ssd1963ParallelDisplayConfig
{
    std::uint16_t      width  = 0;
    std::uint16_t      height = 0;
    Ssd1963PanelTiming timing{};

    /// D0..D7 on pins 0..7 of this port.
    GPIO_TypeDef* data_port = nullptr;
    GpioPin       rs;
    GpioPin       wr;
    GpioPin       cs;
    GpioPin       rd;              ///< optional
    GpioPin       reset;           ///< optional
    GpioPin       display_enable;  ///< optional panel supply gate
};

/// SSD1963 controller on an 8-bit 8080 bus driven by GPIO, RGB565 input.
class Ssd1963ParallelDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit Ssd1963ParallelDisplay(Ssd1963ParallelDisplayConfig config) : config_(config) {}

    Status                                          begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    Status set_rotation(brick::interfaces::display::Rotation rotation) override;
    Status draw_buffer(brick::interfaces::display::DisplayRect        area,
                       const brick::interfaces::display::PixelBuffer& buffer) override;

private:
    void   power_up_panel_() const;
    void   write_bus_(std::uint8_t value) const;
    void   pulse_write_() const;
    void   command_(std::uint8_t value) const;
    void   data_(std::uint8_t value) const;
    void   data16_(std::uint16_t value) const;
    Status initialize_controller_();
    Status set_window_(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height) const;

    Ssd1963ParallelDisplayConfig config_;
    bool                         started_ = false;
};

}  // namespace brick::platform::stm32::f1
