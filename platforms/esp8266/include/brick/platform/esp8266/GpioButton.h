#pragma once

#include <Arduino.h>

#include <cstdint>

#include "brick/interfaces/input/IButton.h"

namespace brick::platform::esp8266
{

struct GpioButtonConfig
{
    std::uint8_t gpio         = 4;
    std::uint8_t pin_mode     = INPUT;
    std::uint8_t active_level = HIGH;
};

class GpioButton final : public brick::interfaces::input::IButton
{
public:
    explicit GpioButton(GpioButtonConfig config);

    bool begin() override;

    bool is_pressed() const override;

private:
    GpioButtonConfig config_;
    bool             initialized_ = false;
};

}  // namespace brick::platform::esp8266
