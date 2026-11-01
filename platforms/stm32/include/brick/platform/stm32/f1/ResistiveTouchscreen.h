#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/core/input/TouchMapper.h"
#include "brick/interfaces/display/ITouchscreen.h"
#include "brick/platform/stm32/f1/Gpio.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

/// A 4-wire resistive panel read with one ADC. X+ and Y+ must be ADC inputs.
struct ResistiveTouchscreenConfig
{
    ADC_TypeDef*  adc = nullptr;
    GpioPin       x_plus;
    GpioPin       x_minus;
    GpioPin       y_plus;
    GpioPin       y_minus;
    std::uint32_t x_plus_channel = ADC_CHANNEL_0;
    std::uint32_t y_plus_channel = ADC_CHANNEL_0;

    brick::interfaces::display::DisplaySize display_size{};
    brick::core::input::TouchCalibration    calibration{};
    /// 12-bit reading of the pulled-up X+ below which the layers touch.
    std::uint16_t pressed_threshold = 819;
};

class ResistiveTouchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    explicit ResistiveTouchscreen(ResistiveTouchscreenConfig config)
        : config_(config), mapper_(config.display_size, config.calibration)
    {
    }

    Status begin() override;
    /// One point, or a release after the last one; Status::no_data when
    /// nothing changed since the last release.
    Status read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;

private:
    bool   layers_touch_();
    Status sample_position_(std::uint16_t& raw_x, std::uint16_t& raw_y);
    Status sample_adc_(std::uint32_t channel, std::uint16_t& value);

    ResistiveTouchscreenConfig      config_;
    brick::core::input::TouchMapper mapper_;
    ADC_HandleTypeDef               adc_{};
    bool                            started_     = false;
    bool                            was_pressed_ = false;
};

}  // namespace brick::platform::stm32::f1
