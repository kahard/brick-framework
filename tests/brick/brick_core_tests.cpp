#include <cassert>

#include "brick/core/image/AssetStreamer.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "brick/core/audio/PeriodicAudioPlayer.h"
#include "brick/core/audio/WavDecoder.h"
#include "brick/core/image/BmpDecoder.h"
#include "brick/core/input/TouchMapper.h"
#include "brick/core/timing/PeriodicTimer.h"
#include "brick/interfaces/display/DisplayCapabilities.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/DisplayRect.h"
#include "brick/interfaces/display/PixelBuffer.h"

namespace
{

class FakeClock final : public brick::interfaces::timing::IClock
{
public:
    std::uint32_t now = 0;
    std::uint32_t millis() const override { return now; }
};

class MemoryFile final : public brick::interfaces::storage::IFile
{
public:
    explicit MemoryFile(const std::vector<std::uint8_t>& data) : data_(data) {}

    std::size_t read(void* buffer, std::size_t size, std::size_t count) override
    {
        const std::size_t requested = size * count;
        const std::size_t available = data_.size() - position_;
        const std::size_t bytes     = requested < available ? requested : available;
        if (bytes > 0)
        {
            std::memcpy(buffer, data_.data() + position_, bytes);
            position_ += bytes;
        }
        return size == 0 ? 0 : bytes / size;
    }

    bool seek(long offset, int origin) override
    {
        long base = 0;
        if (origin == SEEK_CUR)
            base = static_cast<long>(position_);
        if (origin == SEEK_END)
            base = static_cast<long>(data_.size());
        const long target = base + offset;
        if (target < 0 || target > static_cast<long>(data_.size()))
            return false;
        position_ = static_cast<std::size_t>(target);
        return true;
    }

private:
    const std::vector<std::uint8_t>& data_;
    std::size_t                      position_ = 0;
};

class FakeDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    bool begin() override { return true; }
    brick::interfaces::display::DisplaySize size() const override { return { 320, 240 }; }
    brick::interfaces::display::PixelFormat pixel_format() const override { return brick::interfaces::display::PixelFormat::rgb565; }
    bool set_rotation(brick::interfaces::display::Rotation) override { return true; }

    bool draw_buffer(brick::interfaces::display::DisplayRect area, const brick::interfaces::display::PixelBuffer& buffer) override
    {
        if (!buffer.valid() || buffer.format != pixel_format() || buffer.stride_bytes != static_cast<std::size_t>(area.width) * 2)
            return false;
        last_area = area;
        last_pixels = buffer.data;
        last_byte_count = buffer.stride_bytes * buffer.height;
        submitted_areas.push_back(area);
        submitted_byte_counts.push_back(last_byte_count);
        return true;
    }

    brick::interfaces::display::DisplayRect last_area{};
    const std::uint8_t* last_pixels = nullptr;
    std::size_t last_byte_count = 0;
    std::vector<brick::interfaces::display::DisplayRect> submitted_areas;
    std::vector<std::size_t> submitted_byte_counts;
};

class MemoryAssetReader final : public brick::interfaces::display::IAssetReader
{
public:
    explicit MemoryAssetReader(const std::vector<std::uint8_t>& data) : data_(data) {}

    bool read(const brick::interfaces::display::ImageAsset& asset, std::size_t offset,
              std::uint8_t* destination, std::size_t bytes) override
    {
        if (destination == nullptr || asset.data != data_.data() || offset + bytes > data_.size())
            return false;
        std::memcpy(destination, data_.data() + offset, bytes);
        offsets.push_back(offset);
        sizes.push_back(bytes);
        return true;
    }

    const std::vector<std::uint8_t>& data_;
    std::vector<std::size_t> offsets;
    std::vector<std::size_t> sizes;
};

class MemoryFileSystem final : public brick::interfaces::storage::IFileSystem
{
public:
    bool mount() override { return true; }

    std::vector<std::string> list_files(const char*) override { return {}; }

    std::unique_ptr<brick::interfaces::storage::IFile> open(const char* path, const char*) override
    {
        const auto it = files.find(path);
        if (it == files.end())
            return nullptr;
        return std::make_unique<MemoryFile>(it->second);
    }

    std::map<std::string, std::vector<std::uint8_t>> files;
};

class FakeAudioOutput final : public brick::interfaces::audio::IAudioOutput
{
public:
    bool begin() override
    {
        started = true;
        return true;
    }
    void write_sample(std::uint8_t sample) override { samples.push_back(sample); }
    void stop() override { stopped = true; }

    bool                      started = false;
    bool                      stopped = false;
    std::vector<std::uint8_t> samples;
};

void test_periodic_timer()
{
    FakeClock                          clock;
    brick::core::timing::PeriodicTimer timer(clock);
    assert(!timer.due(100));
    clock.now = 99;
    assert(!timer.due(100));
    clock.now = 100;
    assert(timer.due(100));
    assert(!timer.due(100));

    clock.now = 0xFFFFFFF0u;
    timer.reset();
    clock.now = 0x00000020u;
    assert(timer.due(48));
}

std::vector<std::uint8_t> make_2x2_bmp()
{
    // 24-bit, bottom-up BMP. Rows are padded to four-byte boundaries.
    std::vector<std::uint8_t> bmp(70, 0);
    bmp[0]  = 'B';
    bmp[1]  = 'M';
    bmp[2]  = 70;
    bmp[10] = 54;
    bmp[14] = 40;
    bmp[18] = 2;
    bmp[22] = 2;
    bmp[26] = 1;
    bmp[28] = 24;

    // Bottom row: blue, white.
    bmp[54] = 255;
    bmp[55] = 0;
    bmp[56] = 0;
    bmp[57] = 255;
    bmp[58] = 255;
    bmp[59] = 255;
    // Top row: red, green.
    bmp[62] = 0;
    bmp[63] = 0;
    bmp[64] = 255;
    bmp[65] = 0;
    bmp[66] = 255;
    bmp[67] = 0;
    return bmp;
}

void test_bmp_decoder()
{
    MemoryFileSystem fs;
    fs.files["test.bmp"]           = make_2x2_bmp();
    std::uint8_t pixels[2 * 2 * 2] = {};
    const bool   decoded           = brick::core::image::BmpDecoder::decode(fs, "test.bmp", pixels, { .target_width = 2, .target_height = 2 });
    assert(decoded);

    // RGB565, native little endian: top-left red, top-right green,
    // bottom-left blue, white.
    const std::uint8_t expected[] = {
        0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xFF, 0xFF,
    };
    assert(std::memcmp(pixels, expected, sizeof(expected)) == 0);
    assert(!brick::core::image::BmpDecoder::decode(fs, "missing.bmp", pixels, { .target_width = 2, .target_height = 2 }));
}

std::vector<std::uint8_t> make_pcm_wav()
{
    std::vector<std::uint8_t> wav(46, 0);
    std::memcpy(wav.data(), "RIFF", 4);
    wav[4] = 38;
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    wav[16] = 16;
    wav[20] = 1;  // PCM
    wav[22] = 1;  // mono
    wav[24] = 0x40;
    wav[25] = 0x1F;  // 8000 Hz
    wav[28] = 0x40;
    wav[29] = 0x1F;  // byte rate
    wav[32] = 1;     // block alignment
    wav[34] = 8;     // bits per sample
    std::memcpy(wav.data() + 36, "data", 4);
    wav[40] = 2;
    wav[44] = 0;
    wav[45] = 255;
    return wav;
}

void test_wav_decoder()
{
    MemoryFileSystem fs;
    fs.files["test.wav"] = make_pcm_wav();
    brick::core::audio::AudioBuffer buffer;
    assert(brick::core::audio::WavDecoder::decode(fs, "test.wav", buffer));
    assert(buffer.sample_rate() == 8000);
    assert(buffer.size() == 2);
    assert(buffer.data()[0] == 0);
    assert(buffer.data()[1] == 255);
    assert(!brick::core::audio::WavDecoder::decode(fs, "missing.wav", buffer));
}

void test_audio_player()
{
    FakeAudioOutput                         output;
    brick::core::audio::PeriodicAudioPlayer player(output);
    brick::core::audio::AudioBuffer         buffer({ 10, 20, 30 }, 8000);
    assert(player.begin());
    assert(player.play(buffer));
    assert(player.playing());
    player.tick();
    player.tick();
    player.tick();
    assert(!player.playing());
    assert(output.samples == std::vector<std::uint8_t>({ 10, 20, 30, 128 }));
    assert(output.stopped);
}

void test_touch_mapper()
{
    using brick::core::input::TouchCalibration;
    using brick::core::input::TouchMapper;
    using brick::interfaces::display::DisplaySize;
    using brick::interfaces::display::TouchState;

    TouchMapper mapper(DisplaySize{ 480, 480 }, TouchCalibration{ 200, 3800, 300, 3900 });
    auto        top_left = mapper.map(0, 200, 300, 100);
    assert(top_left.x == 0);
    assert(top_left.y == 0);
    assert(top_left.pressure == 100);
    assert(top_left.state == TouchState::moved);

    auto bottom_right = mapper.map(0, 3800, 3900);
    assert(bottom_right.x == 479);
    assert(bottom_right.y == 479);

    TouchMapper inverted(DisplaySize{ 100, 50 }, TouchCalibration{ 0, 1000, 0, 1000, true, true, false });
    auto        point = inverted.map(1, 250, 750);
    assert(point.x == 75);
    assert(point.y == 13);

    TouchMapper swapped(DisplaySize{ 200, 100 }, TouchCalibration{ 0, 1000, 0, 2000, false, false, true });
    auto        swapped_point = swapped.map(2, 500, 1000);
    assert(swapped_point.x == 99);
    assert(swapped_point.y == 49);
}

void test_asset_streamer()
{
    using brick::interfaces::display::DisplayRect;
    using brick::interfaces::display::ImageAsset;
    using brick::interfaces::display::PixelFormat;

    std::vector<std::uint8_t> pixels(40);
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<std::uint8_t>(i);

    FakeDisplay display;
    MemoryAssetReader reader(pixels);
    brick::core::image::AssetStreamer streamer(display, reader);
    const ImageAsset asset{pixels.data(), 4, 5, 8, pixels.size(), PixelFormat::rgb565};
    std::uint8_t scratch[16] = {};

    assert(streamer.stream(asset, DisplayRect{10, 20, 4, 5}, scratch, sizeof(scratch)));
    assert((reader.offsets == std::vector<std::size_t>{0, 16, 32}));
    assert((reader.sizes == std::vector<std::size_t>{16, 16, 8}));
    assert(display.submitted_areas.size() == 3);
    assert(display.submitted_areas[0].y == 20 && display.submitted_areas[0].height == 2);
    assert(display.submitted_areas[1].y == 22 && display.submitted_areas[1].height == 2);
    assert(display.submitted_areas[2].y == 24 && display.submitted_areas[2].height == 1);
    assert((display.submitted_byte_counts == std::vector<std::size_t>{16, 16, 8}));
    assert(!streamer.stream(asset, DisplayRect{0, 0, 4, 5}, scratch, 7));

    std::uint8_t framebuffer_data[40] = {};
    const brick::interfaces::display::WritablePixelBuffer framebuffer{
        framebuffer_data, 4, 5, 8, PixelFormat::rgb565, false};
    reader.offsets.clear();
    reader.sizes.clear();
    assert(streamer.stream_to_buffer(asset, framebuffer, scratch, sizeof(scratch)));
    assert((reader.offsets == std::vector<std::size_t>{0, 16, 32}));
    assert(std::memcmp(framebuffer_data, pixels.data(), pixels.size()) == 0);
}

}  // namespace

int main()
{
    using brick::interfaces::display::DisplayCapabilities;
    using brick::interfaces::display::DisplayRect;
    using brick::interfaces::display::PixelBuffer;

    constexpr DisplayRect screen{ 0, 0, 320, 240 };
    assert(screen.contains(DisplayRect{ 10, 20, 100, 50 }));
    assert(!screen.contains(DisplayRect{ 300, 20, 30, 50 }));
    const DisplayRect empty_rect{ 0, 0, 0, 10 };
    assert(empty_rect.empty());

    const std::uint8_t pixels[64] = {};
    const PixelBuffer partial{ pixels, 8, 4, 20, brick::interfaces::display::PixelFormat::rgb565, true };
    assert(partial.valid());
    assert(partial.stride_bytes > partial.width * 2);

    const DisplayCapabilities capabilities{};
    assert(capabilities.max_buffer_count == 1);

    FakeDisplay display;
    const PixelBuffer packed{ pixels, 8, 4, 16, brick::interfaces::display::PixelFormat::rgb565, false };
    assert(display.draw_buffer({ 12, 30, 8, 4 }, packed));
    assert(display.last_area.x == 12 && display.last_area.y == 30);
    assert(display.last_pixels == pixels && display.last_byte_count == 64);
    const PixelBuffer padded{ pixels, 8, 4, 20, brick::interfaces::display::PixelFormat::rgb565, false };
    assert(!display.draw_buffer({ 12, 30, 8, 4 }, padded));

    test_periodic_timer();
    test_bmp_decoder();
    test_wav_decoder();
    test_audio_player();
    test_touch_mapper();
    test_asset_streamer();
    std::puts("BRICK PC tests passed");
    return 0;
}
