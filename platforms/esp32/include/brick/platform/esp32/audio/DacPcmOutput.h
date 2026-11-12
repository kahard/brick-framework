#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/audio/IPcmOutput.h"
#include "driver/dac_continuous.h"

namespace brick::platform::esp32::audio
{

struct DacPcmOutputConfig
{
    /// DAC_CHANNEL_MASK_CH0 (GPIO25) or DAC_CHANNEL_MASK_CH1 (GPIO26, the CYD's
    /// speaker amplifier).
    dac_channel_mask_t channel  = DAC_CHANNEL_MASK_CH1;
    std::uint32_t      desc_num = 4;
    /// Bytes per DMA buffer; the ESP32 moves the DAC samples in 16-bit slots,
    /// so a buffer holds buf_size / 2 samples (at most 512).
    std::size_t  buf_size   = 512;
    std::uint8_t volume     = 60;
    std::uint8_t full_scale = 100;  ///< share of the DAC range (%) at volume 100
};

/// IPcmOutput over the classic ESP32's 8-bit DAC with DMA (dac_continuous,
/// APLL clock: any audio rate). Stereo is mixed to mono; the volume scales
/// the samples.
///
/// The DMA runs in a ring the whole time the output is started: write()
/// queues samples into a ring buffer without waiting and the DMA interrupt
/// copies them into the buffer it just played, or holds the last level when
/// none are queued (no repeated sound, no gap). start() and stop() ramp the
/// output between 0 V and the mid level in 20 ms, so the amplifier does not
/// click.
class DacPcmOutput final : public brick::interfaces::audio::IPcmOutput
{
public:
    explicit DacPcmOutput(DacPcmOutputConfig config = {});
    ~DacPcmOutput() override { stop(); }
    DacPcmOutput(const DacPcmOutput&)            = delete;
    DacPcmOutput& operator=(const DacPcmOutput&) = delete;

    Status       start(const brick::interfaces::audio::PcmFormat& format) override;
    Status       write(const std::int16_t* frames, std::size_t count, std::size_t& written) override;
    Status       set_volume(std::uint8_t percent) override;
    std::uint8_t volume() const override { return volume_; }
    void         stop() override;
    brick::interfaces::audio::PcmFormat format() const override { return format_; }

private:
    static constexpr std::size_t kRingSize       = 2048;  ///< samples, a power of two
    static constexpr std::size_t kMaxBufferBytes = 1024;

    static bool on_buffer_done_(dac_continuous_handle_t handle, const dac_event_data_t* event, void* user_data);
    void        fill_(std::uint8_t* dma_buffer, std::size_t size);
    std::size_t queued_() const
    {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    void push_(std::uint8_t sample);
    void ramp_to_(std::uint8_t level);
    void wait_played_();

    DacPcmOutputConfig                            config_;
    dac_continuous_handle_t                       handle_ = nullptr;
    brick::interfaces::audio::PcmFormat           format_{ 0, 1 };
    std::uint8_t                                  volume_;
    std::uint8_t                                  level_ = 0;  ///< the last sample queued
    std::uint8_t                                  held_  = 0;  ///< the last sample played (interrupt side)
    std::atomic<std::uint32_t>                    head_{ 0 };  ///< samples queued so far
    std::atomic<std::uint32_t>                    tail_{ 0 };  ///< samples handed to the DMA so far
    std::array<std::uint8_t, kRingSize>           ring_{};
    std::array<std::uint8_t, kMaxBufferBytes / 2> staging_{};
};

}  // namespace brick::platform::esp32::audio
