#pragma once

#include "brick/boards/common/Capabilities.h"
#include "brick/boards/common/Logger.h"
#include "brick/boards/common/Peripheral.h"
#include "brick/boards/esp32/s3/panel480/Profile.h"
#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/esp32/EspIdfLogger.h"
#include "brick/platform/esp32/FreeRtosTime.h"
#include "brick/platform/esp32/SdSpiFileSystem.h"
#include "brick/platform/esp32/s3/St7701sRgbDisplay.h"
#include "brick/platform/esp32/touch/Gt911Touchscreen.h"

namespace brick::boards
{

/// Peripherals compiled into a Panel480 board. Derive from this struct and
/// override only the flags a project needs to change.
struct Panel480DefaultFeatures
{
    static constexpr bool display   = true;
    static constexpr bool touch     = true;
    static constexpr bool sd        = false;
    static constexpr bool logging   = true;
    static constexpr int  log_level = 0;
};

/// 4-inch 480x480 ESP32-S3 panel. begin() starts the display and touch; the
/// SD card is mounted on demand through sd().
template <typename Features = Panel480DefaultFeatures>
class Panel480BoardTemplate final : public brick::interfaces::board::IBoard
{
public:
    explicit Panel480BoardTemplate(
        brick::platform::esp32::s3::St7701sRgbPanelConfig display_config = panel480::display_config())
        : logger_(Features::log_level)
    {
        display_.emplace(display_config);
        touch_.emplace(panel480::touch_config());
        sd_.emplace(panel480::sd_config());
    }

    /// Display configuration with two frame buffers, for the constructor.
    static brick::platform::esp32::s3::St7701sRgbPanelConfig double_buffered_config()
    {
        return panel480::double_buffered_display_config();
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor_static()
    {
        using brick::boards::common::capability_if;
        using brick::interfaces::board::Capability;
        return { "480x480 panel", "ESP32-S3",
                 capability_if(Features::display, Capability::display)
                     | capability_if(Features::touch, Capability::touchscreen)
                     | capability_if(Features::sd, Capability::sd_card) };
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override { return descriptor_static(); }

    bool begin() override
    {
        bool ok = display_.begin();
        ok      = touch_.begin() && ok;
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

    brick::platform::esp32::s3::St7701sRgbDisplay&   display() { return display_.get(); }
    brick::platform::esp32::touch::Gt911Touchscreen& touch() { return touch_.get(); }
    brick::platform::esp32::SdSpiFileSystem&         sd() { return sd_.get(); }
    brick::interfaces::time::ITimeProvider&          time() { return time_; }
    brick::interfaces::logging::ILogger&             logger() { return logger_.get(); }

private:
    template <bool Enabled, typename T>
    using Peripheral = brick::boards::common::Peripheral<Enabled, T>;

    brick::platform::esp32::FreeRtosTime time_;
    brick::boards::common::Logger<Features::logging, brick::platform::esp32::EspIdfLogger,
                                  brick::platform::esp32::NullLogger>
                                                                                 logger_;
    Peripheral<Features::display, brick::platform::esp32::s3::St7701sRgbDisplay>   display_;
    Peripheral<Features::touch, brick::platform::esp32::touch::Gt911Touchscreen>   touch_;
    Peripheral<Features::sd, brick::platform::esp32::SdSpiFileSystem>              sd_;
};

using Panel480Board = Panel480BoardTemplate<>;

}  // namespace brick::boards
