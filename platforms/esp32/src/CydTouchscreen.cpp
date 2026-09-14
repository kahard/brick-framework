#include "brick/platform/esp32/wroom32/CydTouchscreen.h"

namespace brick::platform::esp32::wroom32
{

CydTouchscreen::CydTouchscreen(touch::Xpt2046Touchscreen& touchscreen, CydSpi3PinMux& pin_mux)
    : touchscreen_(touchscreen), pin_mux_(pin_mux)
{
}

bool CydTouchscreen::begin()
{
    return pin_mux_.select_touch() && touchscreen_.begin();
}

bool CydTouchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count)
{
    return pin_mux_.select_touch() && touchscreen_.read(points, capacity, count);
}

}  // namespace brick::platform::esp32::wroom32
