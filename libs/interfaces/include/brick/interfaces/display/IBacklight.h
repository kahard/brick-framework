#pragma once

#include <cstdint>

#include "brick/Status.h"

namespace brick::interfaces::display
{

class IBacklight
{
public:
    virtual ~IBacklight() = default;

    virtual Status       set_brightness_percent(std::uint8_t percent) = 0;
    virtual std::uint8_t brightness_percent() const                   = 0;
};

}  // namespace brick::interfaces::display
