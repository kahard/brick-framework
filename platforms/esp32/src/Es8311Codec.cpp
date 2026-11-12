#include "brick/platform/esp32/audio/Es8311Codec.h"

#include "esp_log.h"

namespace brick::platform::esp32::audio
{

namespace
{
    constexpr char kTag[] = "brick_es8311";

    // Registers (ES8311 user guide).
    constexpr std::uint8_t kReset     = 0x00;
    constexpr std::uint8_t kClock1    = 0x01;
    constexpr std::uint8_t kClock2    = 0x02;
    constexpr std::uint8_t kClock3    = 0x03;
    constexpr std::uint8_t kClock4    = 0x04;
    constexpr std::uint8_t kClock5    = 0x05;
    constexpr std::uint8_t kClock6    = 0x06;
    constexpr std::uint8_t kClock7    = 0x07;
    constexpr std::uint8_t kClock8    = 0x08;
    constexpr std::uint8_t kSdpIn     = 0x09;
    constexpr std::uint8_t kSystem0B  = 0x0B;
    constexpr std::uint8_t kSystem0C  = 0x0C;
    constexpr std::uint8_t kSystem0D  = 0x0D;
    constexpr std::uint8_t kSystem0E  = 0x0E;
    constexpr std::uint8_t kSystem10  = 0x10;
    constexpr std::uint8_t kSystem11  = 0x11;
    constexpr std::uint8_t kSystem12  = 0x12;
    constexpr std::uint8_t kSystem13  = 0x13;
    constexpr std::uint8_t kSystem14  = 0x14;
    constexpr std::uint8_t kAdc15     = 0x15;
    constexpr std::uint8_t kAdc16     = 0x16;
    constexpr std::uint8_t kAdc17     = 0x17;
    constexpr std::uint8_t kAdc1B     = 0x1B;
    constexpr std::uint8_t kAdc1C     = 0x1C;
    constexpr std::uint8_t kDac31     = 0x31;
    constexpr std::uint8_t kDacVolume = 0x32;
    constexpr std::uint8_t kDac37     = 0x37;
    constexpr std::uint8_t kGpio44    = 0x44;
    constexpr std::uint8_t kGp45      = 0x45;

    /// Clock dividers for MCLK = 256 x fs (pre_div and pre_multi 1, ADC/DAC
    /// div 1, single speed, LRCK 256, BCLK div 4).
    struct Coefficients
    {
        std::uint32_t rate;
        std::uint8_t  dac_osr;
    };
    constexpr Coefficients kRates[] = {
        { 8000, 0x20 },  { 16000, 0x20 }, { 22050, 0x10 }, { 24000, 0x10 },
        { 32000, 0x10 }, { 44100, 0x10 }, { 48000, 0x10 },
    };
    constexpr std::uint8_t kAdcOsr   = 0x10;
    constexpr std::uint8_t kLrckHigh = 0x00;
    constexpr std::uint8_t kLrckLow  = 0xFF;
    constexpr std::uint8_t kBclkDiv  = 0x04;
    constexpr std::uint8_t kTopGain  = 0xAB;  ///< -10 dB, the loudest set_volume() goes
}  // namespace

Status Es8311Codec::update_(std::uint8_t reg, std::uint8_t keep_mask, std::uint8_t bits)
{
    std::uint8_t value = 0;
    if (const Status status = registers_.read(reg, value); !is_ok(status))
        return status;
    return write_(reg, static_cast<std::uint8_t>((value & keep_mask) | bits));
}

Status Es8311Codec::clocks_(std::uint32_t sample_rate)
{
    const Coefficients* found = nullptr;
    for (const Coefficients& entry : kRates)
        if (entry.rate == sample_rate)
            found = &entry;
    if (found == nullptr)
        return Status::not_supported;
    Status status = update_(kClock2, 0x07, 0x00);                              // pre_div 1, pre_multi 1
    status        = is_ok(status) ? write_(kClock5, 0x00) : status;            // ADC and DAC div 1
    status        = is_ok(status) ? update_(kClock3, 0x80, kAdcOsr) : status;  // single speed
    status        = is_ok(status) ? update_(kClock4, 0x80, found->dac_osr) : status;
    status        = is_ok(status) ? update_(kClock7, 0xC0, kLrckHigh) : status;
    status        = is_ok(status) ? write_(kClock8, kLrckLow) : status;
    return is_ok(status) ? update_(kClock6, 0xE0, kBclkDiv - 1) : status;
}

Status Es8311Codec::configure(std::uint32_t sample_rate)
{
    std::uint8_t probe = 0;
    if (const Status status = registers_.read(kSystem0D, probe); !is_ok(status))
    {
        ESP_LOGE(kTag, "No ES8311 answers on I2C: %s", to_string(status));
        return status;
    }
    // Power-up sequence of the Espressif ES8311 driver (esp_codec_dev).
    const std::uint8_t start[][2] = {
        { kSystem0D, 0xFA }, { kGpio44, 0x08 },   { kClock1, 0x30 }, { kClock2, 0x00 },   { kClock3, 0x10 },
        { kAdc16, 0x24 },    { kClock4, 0x10 },   { kClock5, 0x00 }, { kSystem0B, 0x00 }, { kSystem0C, 0x00 },
        { kSystem10, 0x1F }, { kSystem11, 0x7F }, { kReset, 0x80 },
    };
    for (const auto& pair : start)
        if (const Status status = write_(pair[0], pair[1]); !is_ok(status))
            return status;
    Status status = update_(kReset, 0xBF, 0x00);                     // slave mode
    status        = is_ok(status) ? write_(kClock1, 0x3F) : status;  // clocks from MCLK on
    status        = is_ok(status) ? update_(kClock6, static_cast<std::uint8_t>(~0x20), 0x00) : status;
    status        = is_ok(status) ? write_(kSystem13, 0x10) : status;
    status        = is_ok(status) ? write_(kAdc1B, 0x0A) : status;
    status        = is_ok(status) ? write_(kAdc1C, 0x6A) : status;
    status        = is_ok(status) ? write_(kGpio44, 0x58) : status;
    status        = is_ok(status) ? update_(kSdpIn, 0xFC, 0x0C) : status;  // 16-bit I2S in
    status        = is_ok(status) ? clocks_(sample_rate) : status;
    if (!is_ok(status))
        return status;
    const std::uint8_t power[][2] = {
        { kReset, 0x80 },    { kClock1, 0x3F },   { kAdc17, 0xBF }, { kSystem0E, 0x02 }, { kSystem12, 0x00 },
        { kSystem14, 0x1A }, { kSystem0D, 0x01 }, { kAdc15, 0x40 }, { kDac37, 0x08 },    { kGp45, 0x00 },
    };
    for (const auto& pair : power)
        if (const Status write_status = write_(pair[0], pair[1]); !is_ok(write_status))
            return write_status;
    status = update_(kSdpIn, static_cast<std::uint8_t>(~0x40), 0x00);  // serial input on
    status = is_ok(status) ? update_(kSystem14, static_cast<std::uint8_t>(~0x40), 0x00) : status;
    return is_ok(status) ? set_muted(true) : status;
}

Status Es8311Codec::set_volume(std::uint8_t percent)
{
    // The DAC gain register: 0x00 mutes, then 0.5 dB per step up to 0xBF =
    // 0 dB (and +32 dB at 0xFF). 1..100 % are -59.5..-10 dB: louder makes the
    // panels' NS4150 amplifier clip (heard from about -7 dB on a JC1060).
    const std::uint32_t level = percent > 100 ? 100 : percent;
    return write_(kDacVolume, level == 0 ? 0 : static_cast<std::uint8_t>(kTopGain - (100 - level)));
}

Status Es8311Codec::set_muted(bool muted)
{
    return update_(kDac31, 0x9F, muted ? 0x60 : 0x00);
}

}  // namespace brick::platform::esp32::audio
