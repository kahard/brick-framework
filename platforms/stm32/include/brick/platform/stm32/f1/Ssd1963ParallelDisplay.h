#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct Ssd1963ParallelDisplayConfig
{
    std::uint16_t width  = 480;
    std::uint16_t height = 272;

    GPIO_TypeDef* data_port           = GPIOA;
    std::uint16_t data_mask           = 0x00FF;
    GPIO_TypeDef* rs_port             = GPIOC;
    std::uint16_t rs_pin              = GPIO_PIN_1;
    GPIO_TypeDef* wr_port             = GPIOC;
    std::uint16_t wr_pin              = GPIO_PIN_2;
    GPIO_TypeDef* rd_port             = GPIOC;
    std::uint16_t rd_pin              = GPIO_PIN_3;
    GPIO_TypeDef* cs_port             = GPIOB;
    std::uint16_t cs_pin              = GPIO_PIN_10;
    GPIO_TypeDef* reset_port          = GPIOB;
    std::uint16_t reset_pin           = GPIO_PIN_2;
    GPIO_TypeDef* display_enable_port = GPIOB;
    std::uint16_t display_enable_pin  = GPIO_PIN_11;

    // SSD1963 values taken from SSD1963HAL_Display_480_272.h in the
    // Termotechnika reference firmware.
    std::uint8_t pll_m = 0x01;
    std::uint8_t pll_n = 0x45;
    std::uint8_t pll_k = 0x47;
};

class Ssd1963ParallelDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit Ssd1963ParallelDisplay(Ssd1963ParallelDisplayConfig config);

    bool                                            begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    bool                                            set_rotation(brick::interfaces::display::Rotation rotation) override;
    bool                                            draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer) override;

private:
    void configure_output_(GPIO_TypeDef* port, std::uint16_t pins) const;
    void write_pin_(GPIO_TypeDef* port, std::uint16_t pin, bool high) const;
    void write_bus_(std::uint8_t value) const;
    void pulse_write_() const;
    void command_(std::uint8_t value) const;
    void data_(std::uint8_t value) const;
    bool initialize_controller_();
    bool set_window_(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height) const;

    Ssd1963ParallelDisplayConfig config_;
    bool                         started_ = false;
};

}  // namespace brick::platform::stm32::f1
