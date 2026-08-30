#include "brick/platform/esp32/LvglTouchAdapter.h"

#include <array>

namespace brick::platform::esp32
{

LvglTouchAdapter::LvglTouchAdapter(brick::interfaces::display::ITouchscreen& touchscreen) : touchscreen_(touchscreen)
{
}

lv_indev_t* LvglTouchAdapter::create()
{
    auto* indev = lv_indev_create();
    if (indev == nullptr)
        return nullptr;
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(indev, this);
    lv_indev_set_read_cb(indev, read_callback_);
    return indev;
}

void LvglTouchAdapter::read_callback_(lv_indev_t* indev, lv_indev_data_t* data)
{
    auto* adapter = static_cast<LvglTouchAdapter*>(lv_indev_get_user_data(indev));
    if (adapter == nullptr)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    std::array<brick::interfaces::display::TouchPoint, 1> points{};
    std::size_t                                           count = 0;
    if (!adapter->touchscreen_.read(points.data(), points.size(), count) || count == 0)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    const auto& point = points[0];
    data->point.x     = point.x;
    data->point.y     = point.y;
    data->state       = point.state == brick::interfaces::display::TouchState::released ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
}

}  // namespace brick::platform::esp32
