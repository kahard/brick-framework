#include "brick/platform/esp32/audio/I2sPcmOutput.h"

#include "brick/platform/esp32/EspStatus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32::audio
{

namespace
{
    constexpr char          kTag[]       = "brick_i2s_audio";
    constexpr std::uint32_t kAmplifierMs = 60;  ///< amplifier settling before the codec unmutes
    constexpr std::uint8_t  kMaxVolume   = 100;
}  // namespace

I2sPcmOutput::I2sPcmOutput(I2sPcmOutputConfig config, II2sCodec* codec)
    : config_(config), codec_(codec), volume_(config.volume > kMaxVolume ? kMaxVolume : config.volume)
{
}

I2sPcmOutput::~I2sPcmOutput()
{
    stop();
    if (channel_ != nullptr)
        i2s_del_channel(channel_);
}

Status I2sPcmOutput::start(const brick::interfaces::audio::PcmFormat& format)
{
    if (format.sample_rate == 0 || format.channels == 0 || format.channels > 2)
        return Status::not_supported;
    if (config_.amplifier_gpio != GPIO_NUM_NC && format_.sample_rate == 0)
    {
        gpio_reset_pin(config_.amplifier_gpio);
        gpio_set_direction(config_.amplifier_gpio, GPIO_MODE_OUTPUT);
        set_amplifier_(false);
    }
    if (const Status status = configure_channel_(format); !is_ok(status))
        return status;
    format_ = format;
    if (codec_ != nullptr)
    {
        // The codec locks to MCLK, which runs now; it comes up muted.
        if (const Status status = codec_->configure(format.sample_rate); !is_ok(status))
        {
            ESP_LOGE(kTag, "Codec setup at %lu Hz failed: %s", static_cast<unsigned long>(format.sample_rate),
                     to_string(status));
            stop();
            return status;
        }
        (void)codec_->set_volume(volume_);
    }
    set_amplifier_(true);
    vTaskDelay(pdMS_TO_TICKS(kAmplifierMs));
    if (codec_ != nullptr)
        (void)codec_->set_muted(false);
    ESP_LOGI(kTag, "Audio out %lu Hz, %u channel(s)", static_cast<unsigned long>(format.sample_rate),
             static_cast<unsigned>(format.channels));
    return Status::ok;
}

Status I2sPcmOutput::configure_channel_(const brick::interfaces::audio::PcmFormat& format)
{
    const i2s_slot_mode_t mode  = format.channels == 2 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    i2s_std_clk_config_t  clock = I2S_STD_CLK_DEFAULT_CONFIG(format.sample_rate);
    clock.mclk_multiple         = config_.mclk_multiple;
    i2s_std_slot_config_t slot  = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, mode);
    if (mode == I2S_SLOT_MODE_MONO)
        slot.slot_mask = I2S_STD_SLOT_LEFT;
    if (channel_ == nullptr)
    {
        i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(config_.port, I2S_ROLE_MASTER);
        channel.auto_clear        = true;  // silence when nothing is queued
        channel.dma_desc_num      = config_.dma_desc_num;
        channel.dma_frame_num     = config_.dma_frame_num;
        if (const esp_err_t error = i2s_new_channel(&channel, &channel_, nullptr); error != ESP_OK)
            return to_status(error);
        i2s_std_config_t standard{};
        standard.clk_cfg       = clock;
        standard.slot_cfg      = slot;
        standard.gpio_cfg.mclk = config_.mclk_gpio;
        standard.gpio_cfg.bclk = config_.bclk_gpio;
        standard.gpio_cfg.ws   = config_.ws_gpio;
        standard.gpio_cfg.dout = config_.dout_gpio;
        standard.gpio_cfg.din  = GPIO_NUM_NC;
        if (const esp_err_t error = i2s_channel_init_std_mode(channel_, &standard); error != ESP_OK)
        {
            i2s_del_channel(channel_);
            channel_ = nullptr;
            return to_status(error);
        }
    }
    else
    {
        if (enabled_)
            ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(channel_));
        enabled_ = false;
        if (const esp_err_t error = i2s_channel_reconfig_std_clock(channel_, &clock); error != ESP_OK)
            return to_status(error);
        if (const esp_err_t error = i2s_channel_reconfig_std_slot(channel_, &slot); error != ESP_OK)
            return to_status(error);
    }
    if (const esp_err_t error = i2s_channel_enable(channel_); error != ESP_OK)
        return to_status(error);
    enabled_ = true;
    return Status::ok;
}

Status I2sPcmOutput::write(const std::int16_t* frames, std::size_t count, std::size_t& written)
{
    written = 0;
    if (!enabled_)
        return Status::not_ready;
    if (frames == nullptr && count > 0)
        return Status::invalid_argument;
    const std::size_t frame_bytes = format_.channels * sizeof(std::int16_t);
    if (codec_ != nullptr || volume_ >= kMaxVolume)
    {
        std::size_t bytes = 0;
        // Timeout 0: copies what fits into the free DMA buffers (whole frames:
        // the buffers hold whole frames) and returns.
        (void)i2s_channel_write(channel_, frames, count * frame_bytes, &bytes, 0);
        written = bytes / frame_bytes;
        return written == 0 && count > 0 ? Status::busy : Status::ok;
    }
    // No codec: scale a piece at a time.
    const std::size_t per_piece = scaled_.size() / format_.channels;
    while (written < count)
    {
        const std::size_t   piece   = count - written < per_piece ? count - written : per_piece;
        const std::size_t   samples = piece * format_.channels;
        const std::int16_t* source  = frames + written * format_.channels;
        for (std::size_t index = 0; index < samples; ++index)
            scaled_[index] = static_cast<std::int16_t>(static_cast<std::int32_t>(source[index]) * volume_ / kMaxVolume);
        std::size_t bytes = 0;
        (void)i2s_channel_write(channel_, scaled_.data(), samples * sizeof(std::int16_t), &bytes, 0);
        written += bytes / frame_bytes;
        if (bytes < samples * sizeof(std::int16_t))
            break;
    }
    return written == 0 && count > 0 ? Status::busy : Status::ok;
}

Status I2sPcmOutput::set_volume(std::uint8_t percent)
{
    volume_ = percent > kMaxVolume ? kMaxVolume : percent;
    if (codec_ != nullptr && format_.sample_rate != 0)
        return codec_->set_volume(volume_);
    return Status::ok;
}

void I2sPcmOutput::stop()
{
    if (codec_ != nullptr && format_.sample_rate != 0)
        (void)codec_->set_muted(true);
    set_amplifier_(false);
    if (channel_ != nullptr && enabled_)
        ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(channel_));
    enabled_            = false;
    format_.sample_rate = 0;
}

void I2sPcmOutput::set_amplifier_(bool on)
{
    if (config_.amplifier_gpio == GPIO_NUM_NC)
        return;
    gpio_set_level(config_.amplifier_gpio, on == config_.amplifier_active_high ? 1 : 0);
}

}  // namespace brick::platform::esp32::audio
