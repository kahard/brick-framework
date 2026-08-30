#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/ITouchscreen.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct ResistiveTouchscreenConfig
{
    std::uint16_t width             = 480;
    std::uint16_t height            = 272;
    std::uint16_t raw_x_min         = 350;
    std::uint16_t raw_x_max         = 3850;
    std::uint16_t raw_y_min         = 800;
    std::uint16_t raw_y_max         = 3600;
    std::uint16_t pressed_threshold = 819;
    bool          invert_x          = false;
    bool          invert_y          = false;
};

class ResistiveTouchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    explicit ResistiveTouchscreen(ResistiveTouchscreenConfig config = {});

    bool begin() override;
    bool read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;

private:
    bool         layers_connected_();
    bool         sample_position_(std::uint16_t& raw_x, std::uint16_t& raw_y);
    bool         sample_adc_(std::uint32_t channel, std::uint16_t& value);
    void         configure_pin_(GPIO_TypeDef* port, std::uint16_t pin, std::uint32_t mode, std::uint32_t pull = GPIO_NOPULL) const;
    std::int16_t map_(std::uint16_t value, std::uint16_t minimum, std::uint16_t maximum, std::uint16_t size, bool invert) const;

    ResistiveTouchscreenConfig config_;
    ADC_HandleTypeDef          adc_{};
    bool                       was_pressed_ = false;
};

}  // namespace brick::platform::stm32::f1
