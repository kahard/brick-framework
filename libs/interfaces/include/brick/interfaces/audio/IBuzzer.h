#pragma once

#include <cstdint>

namespace brick::interfaces::audio
{

class IBuzzer
{
public:
    virtual ~IBuzzer()                           = default;
    virtual bool begin()                         = 0;
    virtual bool set_enabled(bool enabled)       = 0;
    virtual bool beep(std::uint16_t duration_ms) = 0;
};

}  // namespace brick::interfaces::audio
