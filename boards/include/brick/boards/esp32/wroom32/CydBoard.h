#pragma once

#include "brick/boards/common/Capabilities.h"
#include "brick/boards/common/Logger.h"
#include "brick/boards/common/Peripheral.h"
#include "brick/boards/esp32/wroom32/cyd/Profile.h"
#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/esp32/EspIdfLogger.h"
#include "brick/platform/esp32/EspMemory.h"
#include "brick/platform/esp32/FreeRtosTime.h"
#include "brick/platform/esp32/Ili9341SpiDisplay.h"
#include "brick/platform/esp32/PwmBacklight.h"
#include "brick/platform/esp32/touch/Xpt2046Touchscreen.h"
#include "brick/platform/esp32/wroom32/CydSdSpiFileSystem.h"
#include "brick/platform/esp32/wroom32/CydSpi3PinMux.h"
#include "brick/platform/esp32/wroom32/CydTouchscreen.h"

namespace brick::boards
{

/// Peripherals compiled into a CYD board. Derive from this struct and
/// override only the flags a project needs to change.
struct CydDefaultFeatures
{
    static constexpr bool display        = true;
    static constexpr bool touch          = true;
    static constexpr bool backlight      = true;
    static constexpr bool sd             = false;
    static constexpr bool requires_psram = false;
    static constexpr bool logging        = true;
    static constexpr int  log_level      = 0;
};

/// ESP32-2432S028R "Cheap Yellow Display". begin() starts the display,
/// backlight and touch; the SD card is mounted on demand through sd(). With
/// the SD card enabled, touch() transparently switches the shared SPI3 pins.
template <typename Features = CydDefaultFeatures>
class CydBoardTemplate final : public brick::interfaces::board::IBoard
{
public:
#if defined(CONFIG_SPIRAM)
    static_assert(!Features::requires_psram || CONFIG_SPIRAM,
                  "Features::requires_psram needs CONFIG_SPIRAM enabled in sdkconfig.defaults");
#else
    static_assert(!Features::requires_psram,
                  "Features::requires_psram needs CONFIG_SPIRAM enabled in sdkconfig.defaults");
#endif

    CydBoardTemplate() : logger_(Features::log_level)
    {
        display_.emplace(cyd::display_config());
        backlight_.emplace(cyd::kBacklightGpio);
        touch_.emplace(cyd::touch_config());
        if constexpr (Features::sd)
        {
            spi3_pin_mux_.emplace(cyd::spi3_pin_mux_config());
            if constexpr (Features::touch)
                muxed_touch_.emplace(touch_.get(), spi3_pin_mux_.get());
            sd_.emplace(cyd::sd_config(), spi3_pin_mux_.get());
        }
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor_static()
    {
        using brick::boards::common::capability_if;
        using brick::interfaces::board::Capability;
        return { "CYD 2.8-inch", "ESP32-WROOM",
                 capability_if(Features::display, Capability::display)
                     | capability_if(Features::touch, Capability::touchscreen)
                     | capability_if(Features::backlight, Capability::backlight)
                     | capability_if(Features::sd, Capability::sd_card) };
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override { return descriptor_static(); }

    /// Starts every peripheral, even after a failure, and returns the first
    /// failure (Status::ok when all started).
    Status begin() override
    {
        const Status results[] = { display_.begin(), backlight_.begin(), touch_.begin() };
        for (const Status status : results)
            if (!is_ok(status))
                return status;
        return Status::ok;
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
            return &touch();
        return nullptr;
    }

    brick::platform::esp32::Ili9341SpiDisplay& display() { return display_.get(); }
    brick::interfaces::display::ITouchscreen&  touch()
    {
        if constexpr (Features::sd)
            return muxed_touch_.get();
        else
            return touch_.get();
    }
    brick::interfaces::display::IBacklight&              backlight() { return backlight_.get(); }
    brick::platform::esp32::wroom32::CydSdSpiFileSystem& sd() { return sd_.get(); }
    brick::interfaces::time::ITimeProvider&              time() { return time_; }
    brick::interfaces::memory::IMemoryProvider&          memory() { return memory_; }
    brick::interfaces::logging::ILogger&                 logger() { return logger_.get(); }

private:
    template <bool Enabled, typename T>
    using Peripheral = brick::boards::common::Peripheral<Enabled, T>;

    brick::platform::esp32::FreeRtosTime time_;
    brick::platform::esp32::EspMemory    memory_;
    brick::boards::common::Logger<Features::logging, brick::platform::esp32::EspIdfLogger,
                                  brick::platform::esp32::NullLogger>
                                                                                                 logger_;
    Peripheral<Features::sd, brick::platform::esp32::wroom32::CydSpi3PinMux>                     spi3_pin_mux_;
    Peripheral<Features::display, brick::platform::esp32::Ili9341SpiDisplay>                     display_;
    Peripheral<Features::backlight, brick::platform::esp32::PwmBacklight>                        backlight_;
    Peripheral<Features::touch, brick::platform::esp32::touch::Xpt2046Touchscreen>               touch_;
    Peripheral<Features::touch && Features::sd, brick::platform::esp32::wroom32::CydTouchscreen> muxed_touch_;
    Peripheral<Features::sd, brick::platform::esp32::wroom32::CydSdSpiFileSystem>                sd_;
};

using CydBoard = CydBoardTemplate<>;

}  // namespace brick::boards
