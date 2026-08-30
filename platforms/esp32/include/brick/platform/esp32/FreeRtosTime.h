#pragma once

#include "brick/interfaces/time/ITimeProvider.h"

namespace brick::platform::esp32
{

class FreeRtosTime final : public brick::interfaces::time::ITimeProvider
{
public:
    std::uint32_t millis() const override;
    std::uint64_t micros() const override;
    void          delay_ms(std::uint32_t milliseconds) override;
};

}  // namespace brick::platform::esp32
