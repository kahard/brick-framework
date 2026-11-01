#pragma once

#include <cstdint>

#include "brick/Status.h"

namespace brick::interfaces::audio
{

class IBuzzer
{
public:
    virtual ~IBuzzer()                             = default;
    virtual Status begin()                         = 0;
    virtual Status set_enabled(bool enabled)       = 0;
    virtual Status beep(std::uint16_t duration_ms) = 0;
};

}  // namespace brick::interfaces::audio
