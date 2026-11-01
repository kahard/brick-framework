#include "brick/platform/esp32/PwmAudioOutput.h"

#include "brick/platform/esp32/EspStatus.h"

namespace brick::platform::esp32
{

PwmAudioOutput::PwmAudioOutput(int pin, int channel, int carrier_hz)
    : pin_(pin), channel_(static_cast<ledc_channel_t>(channel)), carrier_hz_(carrier_hz)
{
}

Status PwmAudioOutput::begin()
{
    if (initialized_)
        return Status::ok;
    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode          = mode_;
    timer_config.duty_resolution     = LEDC_TIMER_8_BIT;
    timer_config.timer_num           = LEDC_TIMER_0;
    timer_config.freq_hz             = carrier_hz_;
    timer_config.clk_cfg             = LEDC_AUTO_CLK;
    if (const esp_err_t err = ledc_timer_config(&timer_config); err != ESP_OK)
        return to_status(err);
    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num              = pin_;
    channel_config.speed_mode            = mode_;
    channel_config.channel               = channel_;
    channel_config.intr_type             = LEDC_INTR_DISABLE;
    channel_config.timer_sel             = LEDC_TIMER_0;
    channel_config.duty                  = 128;
    channel_config.hpoint                = 0;
    if (const esp_err_t err = ledc_channel_config(&channel_config); err != ESP_OK)
        return to_status(err);
    initialized_ = true;
    return Status::ok;
}

void PwmAudioOutput::write_sample(std::uint8_t sample)
{
    if (!initialized_)
        return;
    ledc_set_duty(mode_, channel_, sample);
    ledc_update_duty(mode_, channel_);
}

void PwmAudioOutput::stop()
{
    if (initialized_)
        ledc_stop(mode_, channel_, 0);
}

}  // namespace brick::platform::esp32
