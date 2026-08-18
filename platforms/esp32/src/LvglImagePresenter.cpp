#include "brick/platform/esp32/LvglImagePresenter.h"

namespace brick::platform::esp32
{

LvglImagePresenter::LvglImagePresenter(lv_obj_t* target) : target_(target)
{
}

void LvglImagePresenter::set_target(lv_obj_t* target)
{
    target_ = target;
}

bool LvglImagePresenter::present(const std::uint8_t* data, std::uint32_t width, std::uint32_t height)
{
    if (target_ == nullptr || data == nullptr || width == 0 || height == 0)
        return false;
    descriptor_.header.w  = width;
    descriptor_.header.h  = height;
    descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor_.data_size = width * height * 2;
    descriptor_.data      = data;
    lv_obj_set_style_bg_img_src(target_, &descriptor_, 0);
    lv_obj_set_style_bg_opa(target_, LV_OPA_COVER, 0);
    lv_obj_invalidate(target_);
    return true;
}

}  // namespace brick::platform::esp32
