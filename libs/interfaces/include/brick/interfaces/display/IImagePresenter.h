#pragma once

#include <cstdint>

#include "brick/Status.h"

namespace brick::interfaces::display
{

class IImagePresenter
{
public:
    virtual ~IImagePresenter()                                                                  = default;
    virtual Status present(const std::uint8_t* data, std::uint32_t width, std::uint32_t height) = 0;
};

}  // namespace brick::interfaces::display
