#pragma once

#include "brick/boards/esp32/detail/Logger.h"
#include "brick/boards/esp32/detail/Peripheral.h"
#include "brick/boards/esp32/p4/jc1060/detail/guition_jc1060p470c_i_w.h"
#include "brick/boards/esp32/p4/jc1060/detail/jc1060_gt911.h"
#include "brick/interfaces/board/BoardDescriptor.h"
#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/esp32/EspIdfLogger.h"
#include "brick/platform/esp32/FreeRtosTime.h"
#include "brick/platform/esp32/p4/MipiDsiDisplay.h"
#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#include "brick/platform/esp32/touch/Gt911Touchscreen.h"
#include "driver/gpio.h"
#include <cstdint>

namespace brick::platform::esp32::p4
{

struct Jc1060DefaultFeatures
{
    static constexpr bool display = true;
    static constexpr bool touch = true;
    static constexpr bool backlight = true;
    static constexpr bool sdmmc = true;
    static constexpr bool logging = true;
    static constexpr int log_level = 0;
};

template <typename Features = Jc1060DefaultFeatures>
class Jc1060BoardTemplate final : public brick::interfaces::board::IBoard
{
  public:
    Jc1060BoardTemplate() : logger_(Features::log_level)
    {
        if constexpr (Features::display)
            display_.emplace(jc1060::detail::guition_jc1060p470c_i_w());
        if constexpr (Features::touch)
            touch_.emplace(jc1060::detail::jc1060_gt911());
        if constexpr (Features::sdmmc)
            sdmmc_.emplace();
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor_static()
    {
        using brick::interfaces::board::Capability;
        return {"JC1060 7-inch", "ESP32-P4",
                (Features::display ? static_cast<std::uint32_t>(Capability::display) : 0U) |
                    (Features::touch ? static_cast<std::uint32_t>(Capability::touchscreen) : 0U) |
                    (Features::backlight ? static_cast<std::uint32_t>(Capability::backlight) : 0U) |
                    (Features::sdmmc ? static_cast<std::uint32_t>(Capability::sd_card) : 0U)};
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override { return descriptor_static(); }

    bool begin() override
    {
        bool ok = true;
        if constexpr (Features::display)
            ok = display_.begin() && ok;
        if constexpr (Features::backlight)
        {
            gpio_set_direction(GPIO_NUM_23, GPIO_MODE_OUTPUT);
            gpio_set_level(GPIO_NUM_23, 1);
        }
        if constexpr (Features::touch)
            ok = touch_.begin() && ok;
        return ok;
    }

    brick::interfaces::display::IDisplayDevice* display_device() override
    {
        if constexpr (Features::display)
            return &display_.get();
        return nullptr;
    }

    brick::interfaces::display::ITouchscreen* touchscreen() override
    {
        if constexpr (Features::touch)
            return &touch_.get();
        return nullptr;
    }

    MipiDsiDisplay& display() { return display_.get(); }
    touch::Gt911Touchscreen& touch() { return touch_.get(); }
    SdmmcFileSystem& sdmmc() { return sdmmc_.get(); }
    brick::interfaces::time::ITimeProvider& time() { return time_; }
    brick::interfaces::logging::ILogger& logger() { return logger_.get(); }

  private:
    brick::platform::esp32::FreeRtosTime time_;
    brick::boards::esp32::detail::Logger<Features::logging> logger_;
    brick::boards::esp32::detail::Peripheral<Features::display, MipiDsiDisplay> display_;
    brick::boards::esp32::detail::Peripheral<Features::touch, touch::Gt911Touchscreen> touch_;
    brick::boards::esp32::detail::Peripheral<Features::sdmmc, SdmmcFileSystem> sdmmc_;
};

} // namespace brick::platform::esp32::p4
