#pragma once

#include <cstdint>
#include <type_traits>

#include "brick/boards/esp32/detail/Logger.h"
#include "brick/boards/esp32/detail/Peripheral.h"
#include "brick/interfaces/board/BoardDescriptor.h"
#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/esp32/FreeRtosTime.h"
#include "brick/platform/esp32/PwmBacklight.h"
#include "brick/platform/esp32/touch/Xpt2046Touchscreen.h"
#include "brick/platform/esp32/wroom32/CydSdSpiFileSystem.h"
#include "brick/platform/esp32/wroom32/CydTouchscreen.h"
#include "brick/platform/esp32/wroom32/profiles/cyd_ili9341_320x240.h"
#include "brick/platform/esp32/wroom32/profiles/cyd_sd_spi.h"
#include "brick/platform/esp32/wroom32/profiles/cyd_xpt2046.h"

namespace brick::platform::esp32
{

struct CydDefaultFeatures
{
    static constexpr bool display = true;
    static constexpr bool touch = true;
    static constexpr bool backlight = true;
    static constexpr bool sd = false;
    static constexpr bool requires_psram = false;
    static constexpr bool logging = true;
    static constexpr int log_level = 0;
};

namespace cyd_detail
{

template <typename Features, typename = void> struct SdEnabled : std::false_type
{
};

template <typename Features>
struct SdEnabled<Features, std::void_t<decltype(Features::sd)>> : std::bool_constant<Features::sd>
{
};

template <typename Features> inline constexpr bool sd_enabled_v = SdEnabled<Features>::value;

template <typename Features, typename = void> struct PsramRequired : std::false_type
{
};

template <typename Features>
struct PsramRequired<Features, std::void_t<decltype(Features::requires_psram)>>
    : std::bool_constant<Features::requires_psram>
{
};

template <typename Features> inline constexpr bool psram_required_v = PsramRequired<Features>::value;

} // namespace cyd_detail

template <typename Features = CydDefaultFeatures> class CydBoardTemplate final : public brick::interfaces::board::IBoard
{
  public:
    static constexpr bool kSdEnabled = cyd_detail::sd_enabled_v<Features>;
    static constexpr bool kRequiresPsram = cyd_detail::psram_required_v<Features>;

#if defined(CONFIG_SPIRAM)
    static_assert(!kRequiresPsram || CONFIG_SPIRAM,
                  "Features::requires_psram needs CONFIG_SPIRAM enabled in sdkconfig.defaults");
#else
    static_assert(!kRequiresPsram, "Features::requires_psram needs CONFIG_SPIRAM enabled in sdkconfig.defaults");
#endif
    CydBoardTemplate() : logger_(Features::log_level)
    {
        if constexpr (Features::display)
            display_.emplace(profiles::cyd_ili9341_320x240());
        if constexpr (Features::backlight)
            backlight_.emplace(GPIO_NUM_21);
        if constexpr (Features::touch)
            touch_.emplace(profiles::cyd_xpt2046());
        if constexpr (kSdEnabled)
        {
            spi3_pin_mux_.emplace(profiles::cyd_spi3_pin_mux());
            if constexpr (Features::touch)
                muxed_touch_.emplace(touch_.get(), spi3_pin_mux_.get());
            sd_.emplace(profiles::cyd_sd_spi(), spi3_pin_mux_.get());
        }
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor_static()
    {
        using brick::interfaces::board::Capability;
        return {"CYD 2.8-inch", "ESP32-WROOM",
                (Features::display ? static_cast<std::uint32_t>(Capability::display) : 0U) |
                    (Features::touch ? static_cast<std::uint32_t>(Capability::touchscreen) : 0U) |
                    (Features::backlight ? static_cast<std::uint32_t>(Capability::backlight) : 0U) |
                    (kSdEnabled ? static_cast<std::uint32_t>(Capability::sd_card) : 0U)};
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override { return descriptor_static(); }

    bool begin() override
    {
        bool ok = true;
        if constexpr (Features::display)
            ok = display_.begin() && ok;
        if constexpr (Features::backlight)
            ok = backlight_.begin() && ok;
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
        {
            if constexpr (kSdEnabled)
                return &muxed_touch_.get();
            return &touch_.get();
        }
        return nullptr;
    }

    Ili9341SpiDisplay& display() { return display_.get(); }
    brick::interfaces::display::ITouchscreen& touch()
    {
        if constexpr (kSdEnabled)
            return muxed_touch_.get();
        return touch_.get();
    }
    brick::interfaces::display::IBacklight& backlight()
    {
        static_assert(Features::backlight, "Enable Features::backlight before accessing the CYD backlight");
        return backlight_.get();
    }
    wroom32::CydSdSpiFileSystem& sd()
    {
        static_assert(kSdEnabled, "Enable Features::sd before accessing the CYD SD card");
        return sd_.get();
    }
    brick::interfaces::time::ITimeProvider& time() { return time_; }
    brick::interfaces::logging::ILogger& logger() { return logger_.get(); }

  private:
    FreeRtosTime time_;
    brick::boards::esp32::detail::Logger<Features::logging> logger_;
    brick::boards::esp32::detail::Peripheral<kSdEnabled, wroom32::CydSpi3PinMux> spi3_pin_mux_;
    brick::boards::esp32::detail::Peripheral<Features::display, Ili9341SpiDisplay> display_;
    brick::boards::esp32::detail::Peripheral<Features::backlight, PwmBacklight> backlight_;
    brick::boards::esp32::detail::Peripheral<Features::touch, touch::Xpt2046Touchscreen> touch_;
    brick::boards::esp32::detail::Peripheral<Features::touch && kSdEnabled, wroom32::CydTouchscreen> muxed_touch_;
    brick::boards::esp32::detail::Peripheral<kSdEnabled, wroom32::CydSdSpiFileSystem> sd_;
};

using CydBoard = CydBoardTemplate<>;

} // namespace brick::platform::esp32
