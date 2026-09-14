#include "brick/platform/esp32/wroom32/CydSpi3PinMux.h"

#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

namespace brick::platform::esp32::wroom32
{

CydSpi3PinMux::CydSpi3PinMux(CydSpi3PinMuxConfig config) : config_(config)
{
}

bool CydSpi3PinMux::select_touch()
{
    gpio_reset_pin(config_.sd_sclk);
    gpio_reset_pin(config_.sd_mosi);
    gpio_set_direction(config_.touch_sclk, GPIO_MODE_OUTPUT);
    gpio_set_direction(config_.touch_mosi, GPIO_MODE_OUTPUT);
    gpio_set_direction(config_.touch_miso, GPIO_MODE_INPUT);
    esp_rom_gpio_connect_out_signal(config_.touch_sclk, VSPICLK_OUT_IDX, false, false);
    esp_rom_gpio_connect_out_signal(config_.touch_mosi, VSPID_OUT_IDX, false, false);
    esp_rom_gpio_connect_in_signal(config_.touch_miso, VSPIQ_IN_IDX, false);
    if (config_.touch_interrupt != GPIO_NUM_NC)
        gpio_intr_enable(config_.touch_interrupt);
    return true;
}

bool CydSpi3PinMux::select_sd()
{
    if (config_.touch_interrupt != GPIO_NUM_NC)
        gpio_intr_disable(config_.touch_interrupt);
    gpio_reset_pin(config_.touch_sclk);
    gpio_reset_pin(config_.touch_mosi);
    gpio_set_direction(config_.sd_sclk, GPIO_MODE_OUTPUT);
    gpio_set_direction(config_.sd_mosi, GPIO_MODE_OUTPUT);
    gpio_set_direction(config_.sd_miso, GPIO_MODE_INPUT);
    gpio_set_pull_mode(config_.sd_miso, GPIO_PULLUP_ONLY);
    esp_rom_gpio_connect_out_signal(config_.sd_sclk, VSPICLK_OUT_IDX, false, false);
    esp_rom_gpio_connect_out_signal(config_.sd_mosi, VSPID_OUT_IDX, false, false);
    esp_rom_gpio_connect_in_signal(config_.sd_miso, VSPIQ_IN_IDX, false);
    return true;
}

}  // namespace brick::platform::esp32::wroom32
