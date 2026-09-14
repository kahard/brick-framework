#pragma once

#include "driver/gpio.h"

namespace brick::platform::esp32::wroom32
{

struct CydSpi3PinMuxConfig
{
    gpio_num_t touch_sclk      = GPIO_NUM_NC;
    gpio_num_t touch_mosi      = GPIO_NUM_NC;
    gpio_num_t touch_miso      = GPIO_NUM_NC;
    gpio_num_t touch_interrupt = GPIO_NUM_NC;
    gpio_num_t sd_sclk         = GPIO_NUM_NC;
    gpio_num_t sd_mosi         = GPIO_NUM_NC;
    gpio_num_t sd_miso         = GPIO_NUM_NC;
};

class CydSpi3PinMux final
{
public:
    explicit CydSpi3PinMux(CydSpi3PinMuxConfig config);

    bool select_touch();
    bool select_sd();

private:
    CydSpi3PinMuxConfig config_;
};

}  // namespace brick::platform::esp32::wroom32
