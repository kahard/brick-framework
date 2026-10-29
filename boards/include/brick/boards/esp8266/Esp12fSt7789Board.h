#pragma once

#include "brick/boards/common/Capabilities.h"
#include "brick/boards/common/Logger.h"
#include "brick/boards/common/Peripheral.h"
#include "brick/boards/esp8266/esp12f_st7789/Profile.h"
#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/esp8266/ArduinoSerialLogger.h"
#include "brick/platform/esp8266/ArduinoTime.h"
#include "brick/platform/esp8266/GpioButton.h"
#include "brick/platform/esp8266/St7789TftDisplay.h"

namespace brick::boards
{

/// Peripherals compiled into an ESP-12F ST7789 board. Derive from this struct
/// and override only the flags a project needs to change.
struct Esp12fSt7789DefaultFeatures
{
    static constexpr bool display   = true;
    static constexpr bool button    = true;
    static constexpr bool logging   = true;
    static constexpr int  log_level = 0;
};

/// ESP-12F with a 240x240 ST7789 display and a TTP223 touch button.
template <typename Features = Esp12fSt7789DefaultFeatures>
class Esp12fSt7789BoardTemplate final : public brick::interfaces::board::IBoard
{
public:
    Esp12fSt7789BoardTemplate() : logger_(Features::log_level)
    {
        display_.emplace(esp12f_st7789::display_config());
        button_.emplace(esp12f_st7789::button_config());
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor_static()
    {
        using brick::boards::common::capability_if;
        using brick::interfaces::board::Capability;
        return { "ESP-12F ST7789 240x240", "ESP8266", capability_if(Features::display, Capability::display) };
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override { return descriptor_static(); }

    bool begin() override
    {
        bool ok = display_.begin();
        ok      = button_.begin() && ok;
        return ok;
    }

    brick::interfaces::display::IDisplayDevice* display_device() override
    {
        if constexpr (Features::display)
            return &display_.get();
        return nullptr;
    }

    brick::interfaces::display::ITouchscreen* touchscreen() override { return nullptr; }

    brick::platform::esp8266::St7789TftDisplay& display() { return display_.get(); }
    brick::platform::esp8266::GpioButton&       button() { return button_.get(); }
    brick::interfaces::time::ITimeProvider&     time() { return time_; }
    brick::interfaces::logging::ILogger&        logger() { return logger_.get(); }

private:
    template <bool Enabled, typename T>
    using Peripheral = brick::boards::common::Peripheral<Enabled, T>;

    brick::platform::esp8266::ArduinoTime time_;
    brick::boards::common::Logger<Features::logging, brick::platform::esp8266::ArduinoSerialLogger,
                                  brick::platform::esp8266::NullLogger>
                                                                              logger_;
    Peripheral<Features::display, brick::platform::esp8266::St7789TftDisplay> display_;
    Peripheral<Features::button, brick::platform::esp8266::GpioButton>        button_;
};

using Esp12fSt7789Board = Esp12fSt7789BoardTemplate<>;

}  // namespace brick::boards
