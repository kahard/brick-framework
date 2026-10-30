#pragma once

#include "brick/interfaces/display/ITouchscreen.h"
#include "lvgl.h"

namespace brick::lvgl
{

class LvglTouchAdapter final
{
public:
    explicit LvglTouchAdapter(brick::interfaces::display::ITouchscreen& touchscreen);

    lv_indev_t* create();

private:
    static void read_callback_(lv_indev_t* indev, lv_indev_data_t* data);

    brick::interfaces::display::ITouchscreen& touchscreen_;
};

}  // namespace brick::lvgl
