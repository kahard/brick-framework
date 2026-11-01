#pragma once

#include "brick/Status.h"
#include "brick/interfaces/board/BoardDescriptor.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/ITouchscreen.h"

namespace brick::interfaces::board
{

class IBoard
{
public:
    virtual ~IBoard()                                                      = default;
    virtual BoardDescriptor                             descriptor() const = 0;
    virtual Status                                      begin()            = 0;
    virtual brick::interfaces::display::IDisplayDevice* display_device()   = 0;
    virtual brick::interfaces::display::ITouchscreen*   touchscreen()      = 0;
};

}  // namespace brick::interfaces::board
