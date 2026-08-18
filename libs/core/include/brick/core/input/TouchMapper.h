#pragma once

#include <cstdint>

#include "brick/interfaces/display/DisplayTypes.h"
#include "brick/interfaces/display/TouchscreenTypes.h"

namespace brick::core::input
{

struct TouchCalibration
{
    std::int16_t raw_x_min = 0;
    std::int16_t raw_x_max = 0;
    std::int16_t raw_y_min = 0;
    std::int16_t raw_y_max = 0;
    bool         invert_x  = false;
    bool         invert_y  = false;
    bool         swap_xy   = false;
};

class TouchMapper
{
public:
    TouchMapper(brick::interfaces::display::DisplaySize display_size, TouchCalibration calibration);

    brick::interfaces::display::TouchPoint map(std::uint8_t id, std::int16_t raw_x, std::int16_t raw_y, std::int16_t pressure = 0) const;

private:
    static std::int16_t normalize(std::int16_t value, std::int16_t minimum, std::int16_t maximum, bool inverted, std::uint16_t output_size);

    brick::interfaces::display::DisplaySize display_size_;
    TouchCalibration                        calibration_;
};

}  // namespace brick::core::input
