#pragma once

#include <cstddef>

#include "brick/interfaces/display/TouchscreenTypes.h"

namespace brick::interfaces::display
{

class ITouchscreen
{
public:
    virtual ~ITouchscreen() = default;

    virtual bool begin() = 0;

    // Returns true when a new sample is available. The implementation writes at
    // most capacity points and returns the number of valid points in count.
    virtual bool read(TouchPoint* points, std::size_t capacity, std::size_t& count) = 0;
};

}  // namespace brick::interfaces::display
