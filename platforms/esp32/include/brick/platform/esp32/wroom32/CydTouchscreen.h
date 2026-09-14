#pragma once

#include "brick/interfaces/display/ITouchscreen.h"
#include "brick/platform/esp32/touch/Xpt2046Touchscreen.h"
#include "brick/platform/esp32/wroom32/CydSpi3PinMux.h"

namespace brick::platform::esp32::wroom32
{

class CydTouchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    CydTouchscreen(touch::Xpt2046Touchscreen& touchscreen, CydSpi3PinMux& pin_mux);

    bool begin() override;
    bool read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;

private:
    touch::Xpt2046Touchscreen& touchscreen_;
    CydSpi3PinMux&             pin_mux_;
};

}  // namespace brick::platform::esp32::wroom32
