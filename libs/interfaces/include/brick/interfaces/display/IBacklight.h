#pragma once

#include <cstdint>

namespace brick::interfaces::display
{

class IBacklight
{
public:
    virtual ~IBacklight() = default;

    virtual bool         set_brightness_percent(std::uint8_t percent) = 0;
    virtual std::uint8_t brightness_percent() const                   = 0;
};

}  // namespace brick::interfaces::display
