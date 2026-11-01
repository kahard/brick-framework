#pragma once

#include <cstddef>

#include "brick/Status.h"
#include "brick/interfaces/display/TouchscreenTypes.h"

namespace brick::interfaces::display
{

class ITouchscreen
{
public:
    virtual ~ITouchscreen() = default;

    virtual Status begin() = 0;

    /// Status::ok when a new sample was read: at most capacity points are written
    /// and count holds how many are valid. Status::no_data when nothing changed
    /// since the last read; other codes report a bus or controller failure.
    virtual Status read(TouchPoint* points, std::size_t capacity, std::size_t& count) = 0;
};

}  // namespace brick::interfaces::display
