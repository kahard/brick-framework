#pragma once

#include <cstdint>

#include "brick/interfaces/audio/IBuzzer.h"
#include "stm32f1xx_hal.h"

namespace brick::platform::stm32::f1
{

struct Stm32BuzzerConfig
{
    GPIO_TypeDef* port        = GPIOB;
    std::uint16_t pin         = GPIO_PIN_8;
    bool          active_high = true;
};

class Stm32Buzzer final : public brick::interfaces::audio::IBuzzer
{
public:
    explicit Stm32Buzzer(Stm32BuzzerConfig config = {}) : config_(config) {}

    bool begin() override;
    bool set_enabled(bool enabled) override;
    bool beep(std::uint16_t duration_ms) override;

private:
    Stm32BuzzerConfig config_;
    bool              initialized_ = false;
};

}  // namespace brick::platform::stm32::f1
