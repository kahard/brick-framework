#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/audio/IPcmOutput.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"

namespace brick::platform::esp32::audio
{

/// An audio codec behind an I2S output (configured over I2C), e.g. ES8311.
class II2sCodec
{
public:
    virtual ~II2sCodec() = default;

    /// Sets the codec up for sample_rate (its MCLK already runs), muted.
    virtual Status configure(std::uint32_t sample_rate) = 0;
    virtual Status set_volume(std::uint8_t percent)     = 0;
    virtual Status set_muted(bool muted)                = 0;
};

struct I2sPcmOutputConfig
{
    i2s_port_t          port          = I2S_NUM_0;
    gpio_num_t          mclk_gpio     = GPIO_NUM_NC;  ///< NC: no master clock (amplifier without a codec)
    gpio_num_t          bclk_gpio     = GPIO_NUM_NC;
    gpio_num_t          ws_gpio       = GPIO_NUM_NC;
    gpio_num_t          dout_gpio     = GPIO_NUM_NC;
    i2s_mclk_multiple_t mclk_multiple = I2S_MCLK_MULTIPLE_256;
    /// Power amplifier enable (e.g. NS4150), NC when there is none to switch.
    gpio_num_t   amplifier_gpio        = GPIO_NUM_NC;
    bool         amplifier_active_high = true;
    std::uint8_t volume                = 60;  ///< initial loudness, 0..100
    /// DMA buffers: dma_desc_num of dma_frame_num frames (6 x 256 frames are
    /// 70 ms at 22.05 kHz).
    std::uint32_t dma_desc_num  = 6;
    std::uint32_t dma_frame_num = 256;
};

/// IPcmOutput over the ESP-IDF I2S standard (Philips) driver, 16-bit mono or
/// stereo, with an optional codec and amplifier enable. write() copies into
/// the DMA buffers without waiting. Without a codec the volume scales the
/// samples. start() blocks for about 100 ms (codec, amplifier settling).
class I2sPcmOutput final : public brick::interfaces::audio::IPcmOutput
{
public:
    explicit I2sPcmOutput(I2sPcmOutputConfig config, II2sCodec* codec = nullptr);
    ~I2sPcmOutput() override;
    I2sPcmOutput(const I2sPcmOutput&)            = delete;
    I2sPcmOutput& operator=(const I2sPcmOutput&) = delete;

    Status       start(const brick::interfaces::audio::PcmFormat& format) override;
    Status       write(const std::int16_t* frames, std::size_t count, std::size_t& written) override;
    Status       set_volume(std::uint8_t percent) override;
    std::uint8_t volume() const override { return volume_; }
    void         stop() override;
    brick::interfaces::audio::PcmFormat format() const override { return format_; }

private:
    Status configure_channel_(const brick::interfaces::audio::PcmFormat& format);
    void   set_amplifier_(bool on);

    I2sPcmOutputConfig                  config_;
    II2sCodec*                          codec_;
    i2s_chan_handle_t                   channel_ = nullptr;
    brick::interfaces::audio::PcmFormat format_{ 0, 1 };
    std::uint8_t                        volume_;
    bool                                enabled_ = false;
    std::array<std::int16_t, 256>       scaled_{};
};

}  // namespace brick::platform::esp32::audio
