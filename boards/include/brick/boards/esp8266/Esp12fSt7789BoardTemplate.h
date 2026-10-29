#pragma once

#include <cstdint>
#include <optional>

#include "brick/interfaces/board/BoardDescriptor.h"
#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/esp8266/ArduinoSerialLogger.h"
#include "brick/platform/esp8266/ArduinoTime.h"
#include "brick/platform/esp8266/GpioButton.h"
#include "brick/platform/esp8266/St7789TftDisplay.h"
#include "brick/platform/esp8266/profiles/esp12f_st7789_240x240_ttp223.h"

namespace brick::platform::esp8266
{

struct Esp12fSt7789DefaultFeatures
{
    static constexpr bool display = true;
    static constexpr bool button = true;
    static constexpr bool logging = true;
    static constexpr int  log_level = 0;
};

namespace esp12f_st7789_detail
{

template <bool Enabled, typename T> class Peripheral;

template <typename T> class Peripheral<true, T>
{
public:
    template <typename... Args> void emplace(Args&&... args) { value_.emplace(static_cast<Args&&>(args)...); }
    bool begin() { return value_.has_value() && value_->begin(); }
    T& get() { return *value_; }

private:
    std::optional<T> value_;
};

template <typename T> class Peripheral<false, T>
{
public:
    template <typename... Args> void emplace(Args&&...) {}
    bool begin() { return true; }
};

template <bool Enabled> class Logger;

template <> class Logger<true>
{
public:
    explicit Logger(int level) : value_(level) {}
    brick::interfaces::logging::ILogger& get() { return value_; }

private:
    ArduinoSerialLogger value_;
};

template <> class Logger<false>
{
public:
    explicit Logger(int) {}
    brick::interfaces::logging::ILogger& get() { return value_; }

private:
    NullLogger value_;
};

}  // namespace esp12f_st7789_detail

template <typename Features = Esp12fSt7789DefaultFeatures> class Esp12fSt7789BoardTemplate final : public brick::interfaces::board::IBoard
{
public:
    Esp12fSt7789BoardTemplate() : logger_(Features::log_level)
    {
        if constexpr (Features::display)
            display_.emplace(profiles::esp12f_st7789_240x240());
        if constexpr (Features::button)
            button_.emplace(profiles::esp12f_ttp223_gpio4());
    }

    static constexpr brick::interfaces::board::BoardDescriptor descriptor_static()
    {
        using brick::interfaces::board::Capability;
        return { "ESP-12F ST7789 240x240", "ESP8266", Features::display ? static_cast<std::uint32_t>(Capability::display) : 0U };
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override { return descriptor_static(); }

    bool begin() override
    {
        bool ok = true;
        if constexpr (Features::display)
            ok = display_.begin() && ok;
        if constexpr (Features::button)
            ok = button_.begin() && ok;
        return ok;
    }

    brick::interfaces::display::IDisplayDevice* display_device() override
    {
        if constexpr (Features::display)
            return &display_.get();
        return nullptr;
    }

    brick::interfaces::display::ITouchscreen* touchscreen() override { return nullptr; }

    St7789TftDisplay& display()
    {
        static_assert(Features::display, "Enable Features::display before accessing the display");
        return display_.get();
    }

    GpioButton& button()
    {
        static_assert(Features::button, "Enable Features::button before accessing the button");
        return button_.get();
    }

    brick::interfaces::time::ITimeProvider& time() { return time_; }
    brick::interfaces::logging::ILogger& logger() { return logger_.get(); }

private:
    ArduinoTime time_;
    esp12f_st7789_detail::Logger<Features::logging> logger_;
    esp12f_st7789_detail::Peripheral<Features::display, St7789TftDisplay> display_;
    esp12f_st7789_detail::Peripheral<Features::button, GpioButton> button_;
};

using Esp12fSt7789Board = Esp12fSt7789BoardTemplate<>;

}  // namespace brick::platform::esp8266
