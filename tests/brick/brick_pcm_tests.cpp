// Tests of the streaming PCM audio of libs/core: tone and WAV sources and
// the player, against a fake output that takes a limited number of frames
// per write (as DMA buffers do).
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "brick/core/audio/PcmSource.h"

namespace
{

using brick::Status;
using brick::core::audio::MemoryBytes;
using brick::core::audio::PcmPlayer;
using brick::core::audio::ToneSource;
using brick::core::audio::Waveform;
using brick::core::audio::WavSource;
using brick::interfaces::audio::PcmFormat;

/// Output with room for room frames per update (the test refills it).
class FakePcmOutput final : public brick::interfaces::audio::IPcmOutput
{
public:
    std::vector<std::int16_t> played;
    std::size_t               room   = 0;
    int                       starts = 0;

    Status start(const PcmFormat& format) override
    {
        format_ = format;
        ++starts;
        return format.sample_rate == 1234 ? Status::not_supported : Status::ok;
    }
    Status write(const std::int16_t* frames, std::size_t count, std::size_t& written) override
    {
        written = 0;
        if (format_.sample_rate == 0)
            return Status::not_ready;
        written = count < room ? count : room;
        room -= written;
        played.insert(played.end(), frames, frames + written * format_.channels);
        return written == 0 ? Status::busy : Status::ok;
    }
    Status set_volume(std::uint8_t percent) override
    {
        volume_ = percent;
        return Status::ok;
    }
    std::uint8_t volume() const override { return volume_; }
    void         stop() override
    {
        format_             = {};
        format_.sample_rate = 0;
    }
    PcmFormat format() const override { return format_; }

private:
    PcmFormat    format_{ 0, 1 };
    std::uint8_t volume_ = 50;
};

std::vector<std::uint8_t> wav(std::uint16_t channels, std::uint16_t bits, std::uint32_t rate,
                              const std::vector<std::uint8_t>& data, bool extra_chunk)
{
    std::vector<std::uint8_t> file;
    const auto                put32 = [&](std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            file.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    const auto put16 = [&](std::uint16_t value)
    {
        file.push_back(static_cast<std::uint8_t>(value));
        file.push_back(static_cast<std::uint8_t>(value >> 8));
    };
    const auto tag = [&](const char* text) { file.insert(file.end(), text, text + 4); };
    tag("RIFF");
    put32(0);
    tag("WAVE");
    if (extra_chunk)
    {
        tag("LIST");
        put32(3);
        file.insert(file.end(), { 'a', 'b', 'c', 0 });  // odd size + pad byte
    }
    tag("fmt ");
    put32(16);
    put16(1);
    put16(channels);
    put32(rate);
    put32(rate * channels * bits / 8);
    put16(static_cast<std::uint16_t>(channels * bits / 8));
    put16(bits);
    tag("data");
    put32(static_cast<std::uint32_t>(data.size()));
    file.insert(file.end(), data.begin(), data.end());
    const std::uint32_t riff = static_cast<std::uint32_t>(file.size() - 8);
    std::memcpy(file.data() + 4, &riff, 4);
    return file;
}

void test_tone_source()
{
    ToneSource tone(1000, 100, 8000, Waveform::sine, 100);
    assert(tone.format().sample_rate == 8000 && tone.format().channels == 1);
    std::vector<std::int16_t> samples(2000);
    std::size_t               count = 0;
    assert(tone.read(samples.data(), samples.size(), count) == Status::ok && count == 800);
    // Fades in from 0, reaches near full scale, fades out to about 0.
    assert(samples[0] == 0);
    int peak = 0;
    for (std::size_t index = 0; index < count; ++index)
        peak = std::abs(samples[index]) > peak ? std::abs(samples[index]) : peak;
    assert(peak > 32000);
    assert(std::abs(samples[count - 1]) < 1000);
    // 1 kHz at 8 kHz: 8 samples per period; after the fade, samples 2 apart differ in sign pattern.
    assert(samples[402] > 30000 && samples[406] < -30000);
    assert(tone.read(samples.data(), samples.size(), count) == Status::no_data && count == 0);
    assert(tone.rewind() == Status::ok && tone.read(samples.data(), 10, count) == Status::ok && count == 10);

    ToneSource square(1000, 10, 8000, Waveform::square, 50);
    assert(square.read(samples.data(), samples.size(), count) == Status::ok && count == 80);
    assert(samples[40] == 16383 || samples[40] == -16383);
}

void test_wav_source()
{
    // 16-bit stereo, a LIST chunk before fmt.
    const std::vector<std::uint8_t> pcm16 = { 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x80, 0xFF, 0x7F };
    std::vector<std::uint8_t>       file  = wav(2, 16, 44100, pcm16, true);
    MemoryBytes                     bytes(file.data(), file.size());
    WavSource                       stereo(bytes);
    assert(stereo.open() == Status::ok);
    assert(stereo.format().sample_rate == 44100 && stereo.format().channels == 2);
    std::int16_t frames[8] = {};
    std::size_t  count     = 0;
    assert(stereo.read(frames, 1, count) == Status::ok && count == 1 && frames[0] == 1 && frames[1] == -1);
    assert(stereo.read(frames, 4, count) == Status::ok && count == 1 && frames[0] == -32768 && frames[1] == 32767);
    assert(stereo.read(frames, 4, count) == Status::no_data);

    // 8-bit mono at an offset inside larger storage.
    const std::vector<std::uint8_t> pcm8  = { 128, 255, 0, 192 };
    std::vector<std::uint8_t>       inner = wav(1, 8, 8000, pcm8, false);
    std::vector<std::uint8_t>       outer(16, 0xAA);
    outer.insert(outer.end(), inner.begin(), inner.end());
    MemoryBytes storage(outer.data(), outer.size());
    WavSource   mono(storage, 16, static_cast<std::uint32_t>(inner.size()));
    assert(mono.open() == Status::ok && mono.duration_ms() == 0 && mono.format().sample_rate == 8000);
    assert(mono.read(frames, 8, count) == Status::ok && count == 4);
    assert(frames[0] == 0 && frames[1] == 127 * 256 && frames[2] == -128 * 256 && frames[3] == 64 * 256);
    assert(mono.rewind() == Status::ok && mono.read(frames, 2, count) == Status::ok && count == 2);

    std::vector<std::uint8_t> bad = wav(1, 24, 8000, pcm8, false);
    MemoryBytes               bad_bytes(bad.data(), bad.size());
    WavSource                 unsupported(bad_bytes);
    assert(unsupported.open() == Status::not_supported);
    const std::uint8_t junk[16] = { 'R', 'I', 'F', 'X' };
    MemoryBytes        junk_bytes(junk, sizeof(junk));
    WavSource          not_wav(junk_bytes);
    assert(not_wav.open() == Status::error);
}

void test_pcm_player()
{
    FakePcmOutput output;
    PcmPlayer     player(output);
    ToneSource    tone(440, 100, 16000);  // 1600 frames
    output.room = 100;
    assert(player.play(tone) == Status::ok && output.starts == 1 && player.playing());
    assert(output.played.size() == 100);
    for (int update = 0; update < 40 && player.playing(); ++update)
    {
        output.room = 100;
        player.update();
    }
    assert(!player.playing() && output.played.size() == 1600);

    // Same format: no restart; a different one restarts the output.
    output.played.clear();
    output.room = 10000;
    assert(tone.rewind() == Status::ok && player.play(tone) == Status::ok && output.starts == 1);
    ToneSource other(440, 10, 22050);
    assert(player.play(other) == Status::ok && output.starts == 2);
    ToneSource odd(440, 10, 1234);
    assert(player.play(odd) == Status::not_supported);
    player.stop();
    assert(!player.playing());
}

}  // namespace

int main()
{
    test_tone_source();
    test_wav_source();
    test_pcm_player();
    std::puts("brick_pcm_tests: all passed");
    return 0;
}
