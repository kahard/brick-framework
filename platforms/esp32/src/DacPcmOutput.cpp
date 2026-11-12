#include "brick/platform/esp32/audio/DacPcmOutput.h"

#include "brick/platform/esp32/EspStatus.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace brick::platform::esp32::audio
{

namespace
{
    constexpr char          kTag[]       = "brick_dac_audio";
    constexpr std::uint8_t  kMaxVolume   = 100;
    constexpr std::uint8_t  kMidLevel    = 128;  ///< the DAC level of a zero sample
    constexpr std::uint32_t kRampMs      = 20;   ///< 0 V <-> mid level at start() and stop()
    constexpr std::uint32_t kDrainWaitMs = 500;  ///< the longest stop() waits for the queue to play
#if CONFIG_DAC_DMA_AUTO_16BIT_ALIGN
    constexpr std::size_t kBytesPerSample = 2;  ///< the driver puts each sample in the high byte of a slot
#else
    constexpr std::size_t kBytesPerSample = 1;
#endif
}  // namespace

DacPcmOutput::DacPcmOutput(DacPcmOutputConfig config)
    : config_(config), volume_(config.volume > kMaxVolume ? kMaxVolume : config.volume)
{
    if (config_.buf_size > kMaxBufferBytes)
        config_.buf_size = kMaxBufferBytes;
    if (config_.full_scale > 100)
        config_.full_scale = 100;
}

Status DacPcmOutput::start(const brick::interfaces::audio::PcmFormat& format)
{
    if (format.sample_rate == 0 || format.channels == 0 || format.channels > 2)
        return Status::not_supported;
    if (handle_ != nullptr && format.sample_rate == format_.sample_rate)
    {
        format_.channels = format.channels;  // the DMA keeps running: only the mixing changes
        return Status::ok;
    }
    stop();
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    level_ = 0;
    held_  = 0;

    dac_continuous_config_t config{};
    config.chan_mask = config_.channel;
    config.desc_num  = config_.desc_num;
    config.buf_size  = config_.buf_size;
    config.freq_hz   = format.sample_rate;
    config.offset    = 0;
    config.clk_src   = DAC_DIGI_CLK_SRC_APLL;  // the default clock starts at 19.6 kHz
    config.chan_mode = DAC_CHANNEL_MODE_SIMUL;
    if (const esp_err_t error = dac_continuous_new_channels(&config, &handle_); error != ESP_OK)
    {
        handle_ = nullptr;
        return to_status(error);
    }
    dac_event_callbacks_t callbacks{};
    callbacks.on_convert_done = on_buffer_done_;
    esp_err_t error           = dac_continuous_register_event_callback(handle_, &callbacks, this);
    if (error == ESP_OK)
        error = dac_continuous_enable(handle_);
    if (error == ESP_OK)
    {
        error = dac_continuous_start_async_writing(handle_);
        if (error != ESP_OK)
            dac_continuous_disable(handle_);
    }
    if (error != ESP_OK)
    {
        dac_continuous_del_channels(handle_);
        handle_ = nullptr;
        return to_status(error);
    }
    format_ = format;
    ramp_to_(kMidLevel);
    ESP_LOGI(kTag, "Audio out %lu Hz on the DAC", static_cast<unsigned long>(format.sample_rate));
    return Status::ok;
}

Status DacPcmOutput::write(const std::int16_t* frames, std::size_t count, std::size_t& written)
{
    written = 0;
    if (handle_ == nullptr)
        return Status::not_ready;
    if (frames == nullptr && count > 0)
        return Status::invalid_argument;
    const std::size_t free  = kRingSize - queued_();
    const std::size_t piece = count < free ? count : free;
    // volume x full scale, out of 100 x 100
    const std::int32_t gain = static_cast<std::int32_t>(volume_) * config_.full_scale;
    for (std::size_t index = 0; index < piece; ++index)
    {
        const std::int16_t* frame  = frames + index * format_.channels;
        std::int32_t        sample = frame[0];
        if (format_.channels == 2)
            sample = (sample + frame[1]) / 2;
        sample = sample * gain / (100 * kMaxVolume);
        push_(static_cast<std::uint8_t>((sample >> 8) + kMidLevel));
    }
    written = piece;
    return written == 0 && count > 0 ? Status::busy : Status::ok;
}

Status DacPcmOutput::set_volume(std::uint8_t percent)
{
    volume_ = percent > kMaxVolume ? kMaxVolume : percent;
    return Status::ok;
}

void DacPcmOutput::stop()
{
    if (handle_ != nullptr)
    {
        ramp_to_(0);
        wait_played_();
        ESP_ERROR_CHECK_WITHOUT_ABORT(dac_continuous_stop_async_writing(handle_));
        ESP_ERROR_CHECK_WITHOUT_ABORT(dac_continuous_disable(handle_));
        ESP_ERROR_CHECK_WITHOUT_ABORT(dac_continuous_del_channels(handle_));
    }
    handle_             = nullptr;
    format_.sample_rate = 0;
}

bool IRAM_ATTR DacPcmOutput::on_buffer_done_(dac_continuous_handle_t, const dac_event_data_t* event, void* user_data)
{
    static_cast<DacPcmOutput*>(user_data)->fill_(static_cast<std::uint8_t*>(event->buf), event->buf_size);
    return false;
}

void IRAM_ATTR DacPcmOutput::fill_(std::uint8_t* dma_buffer, std::size_t size)
{
    std::size_t samples = size / kBytesPerSample;
    if (samples > staging_.size())
        samples = staging_.size();
    std::uint32_t       tail = tail_.load(std::memory_order_relaxed);
    const std::uint32_t head = head_.load(std::memory_order_acquire);
    for (std::size_t index = 0; index < samples; ++index)
    {
        if (tail != head)
            held_ = ring_[tail++ & (kRingSize - 1)];
        staging_[index] = held_;  // nothing queued: hold the level (silence)
    }
    tail_.store(tail, std::memory_order_release);
    (void)dac_continuous_write_asynchronously(handle_, dma_buffer, size, staging_.data(), samples, nullptr);
}

void DacPcmOutput::push_(std::uint8_t sample)
{
    const std::uint32_t head      = head_.load(std::memory_order_relaxed);
    ring_[head & (kRingSize - 1)] = sample;
    head_.store(head + 1, std::memory_order_release);
    level_ = sample;
}

void DacPcmOutput::ramp_to_(std::uint8_t level)
{
    const std::int32_t from  = level_;
    const std::int32_t steps = static_cast<std::int32_t>(format_.sample_rate * kRampMs / 1000);
    const TickType_t   start = xTaskGetTickCount();
    for (std::int32_t step = 1; step <= steps; ++step)
    {
        while (queued_() >= kRingSize)
        {
            if (xTaskGetTickCount() - start > pdMS_TO_TICKS(kDrainWaitMs))
                return;
            vTaskDelay(1);
        }
        push_(static_cast<std::uint8_t>(from + (level - from) * step / steps));
    }
}

void DacPcmOutput::wait_played_()
{
    const TickType_t start = xTaskGetTickCount();
    while (queued_() > 0 && xTaskGetTickCount() - start <= pdMS_TO_TICKS(kDrainWaitMs))
        vTaskDelay(1);
    // The DMA buffers already filled play out too.
    const std::uint32_t buffered = config_.desc_num * (config_.buf_size / kBytesPerSample);
    vTaskDelay(pdMS_TO_TICKS(buffered * 1000 / format_.sample_rate + 2));
}

}  // namespace brick::platform::esp32::audio
