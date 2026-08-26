#pragma once

#include <cstdint>

namespace brick::interfaces::display
{

class IImagePresenter
{
public:
    virtual ~IImagePresenter()                                                                = default;
    virtual bool present(const std::uint8_t* data, std::uint32_t width, std::uint32_t height) = 0;
};

}  // namespace brick::interfaces::display
