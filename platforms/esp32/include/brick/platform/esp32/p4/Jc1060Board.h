#pragma once

#include "brick/interfaces/board/BoardDescriptor.h"
#include "brick/platform/esp32/p4/Jc1060BoardConfig.h"
#if BRICK_JC1060_ENABLE_DISPLAY
#include "brick/platform/esp32/p4/MipiDsiDisplay.h"
#include "brick/platform/esp32/p4/profiles/guition_jc1060p470c_i_w.h"
#endif
#if BRICK_JC1060_ENABLE_TOUCH
#include "brick/platform/esp32/touch/Gt911Touchscreen.h"
#include "brick/platform/esp32/p4/profiles/jc1060_gt911.h"
#endif
#if BRICK_JC1060_ENABLE_SDMMC
#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#endif
#include "driver/gpio.h"

namespace brick::platform::esp32::p4
{

struct Jc1060Pins
{
    gpio_num_t backlight = GPIO_NUM_23;
};

class Jc1060Board
{
public:
    Jc1060Board()
#if BRICK_JC1060_ENABLE_DISPLAY
        : display_(profiles::guition_jc1060p470c_i_w())
#if BRICK_JC1060_ENABLE_TOUCH
        , touch_(profiles::jc1060_gt911())
#endif
#elif BRICK_JC1060_ENABLE_TOUCH
        : touch_(profiles::jc1060_gt911())
#endif
    {
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor()
    {
        using brick::interfaces::board::Capability;
        return {"JC1060 7-inch", "ESP32-P4", (BRICK_JC1060_ENABLE_DISPLAY ? static_cast<std::uint32_t>(Capability::display) : 0U) |
                                                     (BRICK_JC1060_ENABLE_TOUCH ? static_cast<std::uint32_t>(Capability::touchscreen) : 0U) |
                                                     (BRICK_JC1060_ENABLE_BACKLIGHT ? static_cast<std::uint32_t>(Capability::backlight) : 0U) |
                                                     (BRICK_JC1060_ENABLE_SDMMC ? static_cast<std::uint32_t>(Capability::sd_card) : 0U)};
    }

    static constexpr Jc1060Pins pins() { return {}; }

    bool begin()
    {
        bool ok = true;
#if BRICK_JC1060_ENABLE_DISPLAY
        ok = ok && display_.begin();
#endif
#if BRICK_JC1060_ENABLE_BACKLIGHT
        gpio_set_direction(pins().backlight, GPIO_MODE_OUTPUT);
        gpio_set_level(pins().backlight, 1);
#endif
#if BRICK_JC1060_ENABLE_TOUCH
        ok = ok && touch_.begin();
#endif
        return ok;
    }

#if BRICK_JC1060_ENABLE_DISPLAY
    MipiDsiDisplay& display() { return display_; }
#endif
#if BRICK_JC1060_ENABLE_TOUCH
    touch::Gt911Touchscreen& touch() { return touch_; }
#endif
#if BRICK_JC1060_ENABLE_SDMMC
    SdmmcFileSystem& sdmmc() { return sdmmc_; }
#endif

private:
#if BRICK_JC1060_ENABLE_DISPLAY
    MipiDsiDisplay display_{profiles::guition_jc1060p470c_i_w()};
#endif
#if BRICK_JC1060_ENABLE_TOUCH
    touch::Gt911Touchscreen touch_{profiles::jc1060_gt911()};
#endif
#if BRICK_JC1060_ENABLE_SDMMC
    SdmmcFileSystem sdmmc_;
#endif
};

}  // namespace brick::platform::esp32::p4
