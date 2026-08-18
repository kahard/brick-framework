#pragma once

#include <cstdint>

#include "brick/interfaces/display/IImagePresenter.h"
#include "lvgl.h"

namespace brick::platform::esp32
{

class LvglImagePresenter final : public interfaces::display::IImagePresenter
{
public:
    explicit LvglImagePresenter(lv_obj_t* target = nullptr);

    void set_target(lv_obj_t* target);

    bool present(const std::uint8_t* data, std::uint32_t width, std::uint32_t height) override;

private:
    lv_obj_t*    target_;
    lv_img_dsc_t descriptor_{};
};

}  // namespace brick::platform::esp32
