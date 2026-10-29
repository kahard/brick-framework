#pragma once

#include "brick/boards/common/Capabilities.h"
#include "brick/boards/common/Logger.h"
#include "brick/boards/common/Peripheral.h"
#include "brick/boards/esp32/p4/jc8012/Profile.h"
#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/esp32/EspIdfLogger.h"
#include "brick/platform/esp32/FreeRtosTime.h"
#include "brick/platform/esp32/GpioBacklight.h"
#include "brick/platform/esp32/p4/MipiDsiDisplay.h"
#include "brick/platform/esp32/p4/SdmmcFileSystem.h"
#include "brick/platform/esp32/touch/Gsl3680Touchscreen.h"

namespace brick::boards
{

/// Peripherals compiled into a JC8012 board. Derive from this struct and
/// override only the flags a project needs to change.
struct Jc8012DefaultFeatures
{
    static constexpr bool display   = true;
    static constexpr bool touch     = true;
    static constexpr bool backlight = true;
    static constexpr bool sd        = false;
    static constexpr bool logging   = true;
    static constexpr int  log_level = 0;
};

/// Guition JC8012P4A1C, 10-inch ESP32-P4 panel. The rotation applies to both
/// the display and the touch coordinates. begin() starts the display,
/// backlight and touch; the SD card is mounted on demand through sd().
template <typename Features = Jc8012DefaultFeatures>
class Jc8012BoardTemplate final : public brick::interfaces::board::IBoard
{
public:
    explicit Jc8012BoardTemplate(
        brick::interfaces::display::Rotation rotation = brick::interfaces::display::Rotation::rotate_0)
        : logger_(Features::log_level)
    {
        display_.emplace(jc8012::display_config(rotation));
        backlight_.emplace(jc8012::kBacklightGpio);
        touch_.emplace(jc8012::touch_config(rotation));
        sd_.emplace(jc8012::sd_config());
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor_static()
    {
        using brick::boards::common::capability_if;
        using brick::interfaces::board::Capability;
        return { "JC8012 10-inch", "ESP32-P4",
                 capability_if(Features::display, Capability::display)
                     | capability_if(Features::touch, Capability::touchscreen)
                     | capability_if(Features::backlight, Capability::backlight)
                     | capability_if(Features::sd, Capability::sd_card) };
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override { return descriptor_static(); }

    bool begin() override
    {
        bool ok = display_.begin();
        ok      = backlight_.begin() && ok;
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

    brick::platform::esp32::p4::MipiDsiDisplay&        display() { return display_.get(); }
    brick::platform::esp32::touch::Gsl3680Touchscreen& touch() { return touch_.get(); }
    brick::interfaces::display::IBacklight&            backlight() { return backlight_.get(); }
    brick::platform::esp32::p4::SdmmcFileSystem&       sd() { return sd_.get(); }
    brick::interfaces::time::ITimeProvider&            time() { return time_; }
    brick::interfaces::logging::ILogger&               logger() { return logger_.get(); }

private:
    template <bool Enabled, typename T>
    using Peripheral = brick::boards::common::Peripheral<Enabled, T>;

    brick::platform::esp32::FreeRtosTime time_;
    brick::boards::common::Logger<Features::logging, brick::platform::esp32::EspIdfLogger,
                                  brick::platform::esp32::NullLogger>
                                                                                   logger_;
    Peripheral<Features::display, brick::platform::esp32::p4::MipiDsiDisplay>      display_;
    Peripheral<Features::backlight, brick::platform::esp32::GpioBacklight>         backlight_;
    Peripheral<Features::touch, brick::platform::esp32::touch::Gsl3680Touchscreen> touch_;
    Peripheral<Features::sd, brick::platform::esp32::p4::SdmmcFileSystem>          sd_;
};

using Jc8012Board = Jc8012BoardTemplate<>;

}  // namespace brick::boards
