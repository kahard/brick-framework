#pragma once

#include <cstdint>

#include "brick/interfaces/audio/IBuzzer.h"
#include "brick/platform/stm32/f1/Gpio.h"

namespace brick::platform::stm32::f1
{

struct Stm32BuzzerConfig
{
    GpioPin pin;
    bool    active_high = true;
};

/// A buzzer with its own oscillator, switched by a GPIO.
class Stm32Buzzer final : public brick::interfaces::audio::IBuzzer
{
public:
    explicit Stm32Buzzer(Stm32BuzzerConfig config) : config_(config) {}

    Status begin() override;
    Status set_enabled(bool enabled) override;
    /// Blocks for duration_ms; a loop that must not block uses set_enabled().
    Status beep(std::uint16_t duration_ms) override;

private:
    Stm32BuzzerConfig config_;
    bool              initialized_ = false;
};

}  // namespace brick::platform::stm32::f1
