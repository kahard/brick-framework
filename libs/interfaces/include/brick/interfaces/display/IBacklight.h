#pragma once

#include <cstdint>

namespace brick::interfaces::display
{

class IBacklight
{
public:
    virtual ~IBacklight() = default;

    virtual bool begin()                              = 0;
    virtual bool set_enabled(bool enabled)            = 0;
    virtual bool set_brightness(std::uint8_t percent) = 0;
};

}  // namespace brick::interfaces::display
