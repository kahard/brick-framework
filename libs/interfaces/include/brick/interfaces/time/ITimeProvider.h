#pragma once

#include <cstdint>

namespace brick::interfaces::time
{

class ITimeProvider
{
public:
    virtual ~ITimeProvider()                                   = default;
    virtual std::uint32_t millis() const                       = 0;
    virtual std::uint64_t micros() const { return static_cast<std::uint64_t>(millis()) * 1000ULL; }
    virtual void          delay_ms(std::uint32_t milliseconds) = 0;
};

}  // namespace brick::interfaces::time
