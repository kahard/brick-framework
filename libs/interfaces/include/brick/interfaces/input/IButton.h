#pragma once

#include "brick/Status.h"

namespace brick::interfaces::input
{

class IButton
{
public:
    virtual ~IButton() = default;

    virtual Status begin()            = 0;
    virtual bool   is_pressed() const = 0;
};

}  // namespace brick::interfaces::input
