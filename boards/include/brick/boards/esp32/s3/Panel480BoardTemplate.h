#pragma once

#include "brick/boards/esp32/detail/Logger.h"
#include "brick/boards/esp32/detail/Peripheral.h"
#include "brick/interfaces/board/BoardDescriptor.h"
#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/esp32/EspIdfLogger.h"
#include "brick/platform/esp32/FreeRtosTime.h"
#include "brick/platform/esp32/SdSpiFileSystem.h"
#include "brick/platform/esp32/s3/St7701sRgbDisplay.h"
#include "brick/platform/esp32/s3/profiles/st7701s_480x480.h"
#include "brick/platform/esp32/s3/profiles/st7701s_gt911.h"
#include "brick/platform/esp32/touch/Gt911Touchscreen.h"
#include <cstdint>

namespace brick::platform::esp32::s3
{

struct Panel480DefaultFeatures
{
    static constexpr bool display = true;
    static constexpr bool touch = true;
    static constexpr bool sd = false;
    static constexpr bool logging = true;
    static constexpr int log_level = 0;
};

template <typename Features = Panel480DefaultFeatures>
class Panel480BoardTemplate final : public brick::interfaces::board::IBoard
{
  public:
    Panel480BoardTemplate() : logger_(Features::log_level)
    {
        if constexpr (Features::display)
            display_.emplace(profiles::st7701s_480x480());
        if constexpr (Features::touch)
            touch_.emplace(profiles::st7701s_gt911());
        if constexpr (Features::sd)
            sd_.emplace(
                brick::platform::esp32::SdSpiFileSystemConfig{GPIO_NUM_42, GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_41});
    }

    explicit Panel480BoardTemplate(St7701sRgbPanelConfig display_config) : logger_(Features::log_level)
    {
        static_assert(Features::display, "A display configuration requires display=true");
        display_.emplace(display_config);
        if constexpr (Features::touch)
            touch_.emplace(profiles::st7701s_gt911());
        if constexpr (Features::sd)
            sd_.emplace(
                brick::platform::esp32::SdSpiFileSystemConfig{GPIO_NUM_42, GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_41});
    }

    static St7701sRgbPanelConfig double_buffered_config()
    {
        static_assert(Features::display, "A framebuffer configuration requires display=true");
        St7701sRgbPanelConfig config = profiles::st7701s_480x480();
        config.frame_buffer_count = 2;
        return config;
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor_static()
    {
        using brick::interfaces::board::Capability;
        return {"480x480 panel", "ESP32-S3",
                (Features::display ? static_cast<std::uint32_t>(Capability::display) : 0U) |
                    (Features::touch ? static_cast<std::uint32_t>(Capability::touchscreen) : 0U) |
                    (Features::sd ? static_cast<std::uint32_t>(Capability::sd_card) : 0U)};
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override { return descriptor_static(); }

    bool begin() override
    {
        bool ok = true;
        if constexpr (Features::display)
            ok = display_.begin() && ok;
        if constexpr (Features::touch)
            ok = touch_.begin() && ok;
        if constexpr (Features::sd)
            ok = sd_.get().mount() && ok;
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

    St7701sRgbDisplay& display() { return display_.get(); }
    touch::Gt911Touchscreen& touch() { return touch_.get(); }
    brick::platform::esp32::SdSpiFileSystem& sd_card() { return sd_.get(); }
    brick::interfaces::time::ITimeProvider& time() { return time_; }
    brick::interfaces::logging::ILogger& logger() { return logger_.get(); }

  private:
    brick::platform::esp32::FreeRtosTime time_;
    brick::boards::esp32::detail::Logger<Features::logging> logger_;
    brick::boards::esp32::detail::Peripheral<Features::display, St7701sRgbDisplay> display_;
    brick::boards::esp32::detail::Peripheral<Features::touch, touch::Gt911Touchscreen> touch_;
    brick::boards::esp32::detail::Peripheral<Features::sd, brick::platform::esp32::SdSpiFileSystem> sd_;
};

} // namespace brick::platform::esp32::s3
