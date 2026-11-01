#include <cassert>

#include "brick/interfaces/board/BoardDescriptor.h"
#include <array>

#include "brick/core/image/AssetBundle.h"
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
#include "brick/core/display/Screen.h"
#include "brick/core/image/BmpDecoder.h"
#include "brick/core/input/TouchMapper.h"
#include "brick/core/timing/PeriodicTimer.h"
#include "brick/interfaces/display/DisplayCapabilities.h"
#include "brick/interfaces/display/DisplayRect.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/PixelBuffer.h"
#include "brick/mocks/Fakes.h"

namespace
{

using brick::mocks::FakeAudioOutput;
using brick::mocks::FakeClock;
using brick::mocks::MemoryAssetReader;
using brick::mocks::MemoryAssetSource;
using brick::mocks::MemoryFile;
using brick::mocks::MemoryFileSystem;
using FakeDisplay = brick::mocks::RecordingDisplay;
using brick::Status;

void test_status()
{
    static_assert(brick::is_ok(Status::ok) && !brick::is_ok(Status::error));
    assert(std::strcmp(brick::to_string(Status::ok), "ok") == 0);
    assert(std::strcmp(brick::to_string(Status::timeout), "timeout") == 0);
    assert(std::strcmp(brick::to_string(Status::not_supported), "not supported") == 0);
}

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
    const Status decoded =
        brick::core::image::BmpDecoder::decode(fs, "test.bmp", pixels, { .target_width = 2, .target_height = 2 });
    assert(decoded == Status::ok && fs.open_files() == 0);

    // RGB565, native little endian: top-left red, top-right green,
    // bottom-left blue, white.
    const std::uint8_t expected[] = {
        0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xFF, 0xFF,
    };
    assert(std::memcmp(pixels, expected, sizeof(expected)) == 0);
    assert(brick::core::image::BmpDecoder::decode(fs, "missing.bmp", pixels, { .target_width = 2, .target_height = 2 })
           == Status::not_found);
}

// Top-down BMP whose pixel (x, y) has red = x, green = y * 4 and blue = 0x80.
std::vector<std::uint8_t> make_gradient_bmp(int width, int height, int bits)
{
    const int                 bytes  = bits / 8;
    const int                 stride = ((width * bytes + 3) / 4) * 4;
    std::vector<std::uint8_t> bmp(54 + static_cast<std::size_t>(stride) * height, 0);
    bmp[0]  = 'B';
    bmp[1]  = 'M';
    bmp[10] = 54;
    bmp[14] = 40;
    bmp[18] = static_cast<std::uint8_t>(width);
    // Negative height: rows are stored top-down.
    const std::int32_t top_down = -height;
    std::memcpy(&bmp[22], &top_down, sizeof(top_down));
    bmp[26] = 1;
    bmp[28] = static_cast<std::uint8_t>(bits);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            std::uint8_t* pixel = &bmp[54 + static_cast<std::size_t>(y) * stride + x * bytes];
            pixel[0]            = 0x80;
            pixel[1]            = static_cast<std::uint8_t>(y * 4);
            pixel[2]            = static_cast<std::uint8_t>(x);
        }
    return bmp;
}

std::uint16_t expected_gradient(int x, int y)
{
    return static_cast<std::uint16_t>(((x & 0xF8) << 8) | (((y * 4) & 0xFC) << 3) | (0x80 >> 3));
}

void test_bmp_decoder_wide_rows()
{
    // Rows wider than the decoder's read chunk, in both pixel sizes, plus a
    // target narrower than the source (left part copied, rows centred).
    for (const int bits : { 24, 32 })
    {
        MemoryFileSystem fs;
        fs.files["wide.bmp"] = make_gradient_bmp(70, 3, bits);
        std::uint8_t pixels[70 * 3 * 2];
        assert(
            brick::core::image::BmpDecoder::decode(fs, "wide.bmp", pixels, { .target_width = 70, .target_height = 3 })
            == Status::ok);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 70; ++x)
            {
                const std::size_t   index = (static_cast<std::size_t>(y) * 70 + x) * 2;
                const std::uint16_t value = static_cast<std::uint16_t>(pixels[index] | (pixels[index + 1] << 8));
                assert(value == expected_gradient(x, y));
            }

        std::uint8_t cropped[40 * 1 * 2];
        assert(
            brick::core::image::BmpDecoder::decode(fs, "wide.bmp", cropped, { .target_width = 40, .target_height = 1 })
            == Status::ok);
        for (int x = 0; x < 40; ++x)
            assert(static_cast<std::uint16_t>(cropped[x * 2] | (cropped[x * 2 + 1] << 8)) == expected_gradient(x, 1));
        assert(fs.open_files() == 0);
    }
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
    fs.files["test.wav"]                       = make_pcm_wav();
    std::uint8_t                    samples[4] = {};
    brick::core::audio::AudioBuffer buffer;
    assert(brick::core::audio::WavDecoder::decode(fs, "test.wav", samples, sizeof(samples), buffer) == Status::ok);
    assert(buffer.sample_rate() == 8000);
    assert(buffer.size() == 2 && buffer.data() == samples);
    assert(buffer.data()[0] == 0);
    assert(buffer.data()[1] == 255);
    assert(fs.open_files() == 0);
    assert(brick::core::audio::WavDecoder::decode(fs, "missing.wav", samples, sizeof(samples), buffer)
           == Status::not_found);
    // More frames than the caller's buffer holds.
    assert(brick::core::audio::WavDecoder::decode(fs, "test.wav", samples, 1, buffer) == Status::no_memory);
    assert(fs.open_files() == 0);
}

void test_audio_player()
{
    FakeAudioOutput                         output;
    brick::core::audio::PeriodicAudioPlayer player(output);
    const std::uint8_t                      samples[] = { 10, 20, 30 };
    brick::core::audio::AudioBuffer         buffer(samples, sizeof(samples), 8000);
    assert(player.begin() == Status::ok);
    assert(player.play(buffer) == Status::ok);
    assert(player.play(brick::core::audio::AudioBuffer{}) == Status::invalid_argument);
    assert(player.playing());
    player.tick();
    player.tick();
    player.tick();
    assert(!player.playing());
    assert(output.samples == std::vector<std::uint8_t>({ 10, 20, 30, 128 }));
    assert(output.stopped);
}

void test_screen_canvas()
{
    FakeDisplay                   display;
    brick::core::display::Screen  screen(display);
    std::array<std::uint16_t, 64> pixels{};

    // The canvas draws into the caller's pixels and presents exactly its area.
    auto canvas = screen.create_canvas({ 4, 8, 16, 4 }, pixels);
    assert(canvas.valid() && canvas.buffer().data == reinterpret_cast<std::uint8_t*>(pixels.data()));
    assert(canvas.clear(0xF800).present() == Status::ok);
    assert(pixels[0] == 0xF800 && pixels[63] == 0xF800);
    assert(display.last_area.x == 4 && display.last_area.y == 8 && display.last_byte_count == 16 * 4 * 2);

    // A full-width strip of the 320-pixel display needs more than 64 pixels.
    assert(!screen.create_canvas(1, pixels).valid());
    assert(!screen.create_canvas({ 0, 0, 16, 5 }, pixels).valid());
    assert(!screen.create_canvas({ 0, 0, 0, 4 }, pixels).valid());
    assert(!screen.create_canvas({ 0, 0, 1, 1 }, nullptr, 1).valid());
    // present() reports why a canvas is invalid.
    assert(screen.create_canvas(1, pixels).clear(0).present() == Status::no_memory);
    assert(screen.create_canvas({ 0, 0, 0, 4 }, pixels).present() == Status::invalid_argument);
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

    FakeDisplay                       display;
    MemoryAssetReader                 reader(pixels);
    brick::core::image::AssetStreamer streamer(display, reader);
    const ImageAsset                  asset{ pixels.data(), 4, 5, 8, pixels.size(), PixelFormat::rgb565 };
    std::uint8_t                      scratch[16] = {};

    assert(streamer.stream(asset, DisplayRect{ 10, 20, 4, 5 }, scratch, sizeof(scratch)) == Status::ok);
    assert((reader.offsets == std::vector<std::size_t>{ 0, 16, 32 }));
    assert((reader.sizes == std::vector<std::size_t>{ 16, 16, 8 }));
    assert(display.submitted_areas.size() == 3);
    assert(display.submitted_areas[0].y == 20 && display.submitted_areas[0].height == 2);
    assert(display.submitted_areas[1].y == 22 && display.submitted_areas[1].height == 2);
    assert(display.submitted_areas[2].y == 24 && display.submitted_areas[2].height == 1);
    assert((display.submitted_byte_counts == std::vector<std::size_t>{ 16, 16, 8 }));
    // 7 bytes of scratch cannot hold one 8-byte row.
    assert(streamer.stream(asset, DisplayRect{ 0, 0, 4, 5 }, scratch, 7) == Status::no_memory);

    std::uint8_t                                          framebuffer_data[40] = {};
    const brick::interfaces::display::WritablePixelBuffer framebuffer{ framebuffer_data,    4,    5, 8,
                                                                       PixelFormat::rgb565, false };
    reader.offsets.clear();
    reader.sizes.clear();
    assert(streamer.stream_to_buffer(asset, framebuffer, scratch, sizeof(scratch)) == Status::ok);
    assert((reader.offsets == std::vector<std::size_t>{ 0, 16, 32 }));
    assert(std::memcmp(framebuffer_data, pixels.data(), pixels.size()) == 0);
}

void test_asset_bundle()
{
    using brick::core::image::AssetBundle;
    using brick::interfaces::display::AssetDescriptor;
    using brick::interfaces::display::AssetId;
    using brick::interfaces::display::PixelFormat;

    const std::array<std::uint8_t, 8> data{ 1, 2, 3, 4, 5, 6, 7, 8 };
    const AssetDescriptor             entries[] = {
        { static_cast<AssetId>(1), 0, 4, 2, 1, 4, PixelFormat::rgb565 },
        { static_cast<AssetId>(2), 4, 4, 2, 1, 4, PixelFormat::rgb565 },
    };
    const AssetBundle bundle(data.data(), data.size(), entries, 2);

    const auto* second = bundle.find(2);
    assert(second != nullptr);
    assert(second->offset == 4);
    const auto image = bundle.image(2);
    assert(image.valid());
    assert(image.data == data.data() + 4);
    assert(image.data_size == 4);
    assert(!bundle.image(99).valid());
}

void test_asset_source_streamer()
{
    using brick::interfaces::display::AssetDescriptor;
    using brick::interfaces::display::DisplayRect;
    using brick::interfaces::display::PixelFormat;

    const std::vector<std::uint8_t>   bundle_data{ 99, 98, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 97 };
    const AssetDescriptor             asset{ 7, 2, 12, 2, 3, 4, PixelFormat::rgb565 };
    MemoryAssetSource                 source(bundle_data);
    FakeDisplay                       display;
    brick::core::image::AssetStreamer streamer(display);
    std::uint8_t                      scratch[4] = {};

    assert(streamer.stream(asset, source, DisplayRect{ 0, 0, 2, 3 }, scratch, sizeof(scratch)) == Status::ok);
    assert((source.offsets == std::vector<std::size_t>{ 0, 4, 8 }));
    assert((source.sizes == std::vector<std::size_t>{ 4, 4, 4 }));
    assert(display.submitted_byte_counts.size() == 3);
    assert(display.submitted_byte_counts[0] == 4 && display.submitted_byte_counts[2] == 4);

    const AssetDescriptor out_of_bounds{ 8, 13, 4, 2, 1, 4, PixelFormat::rgb565 };
    // The source refuses a read beyond the bundle; the streamer passes its status on.
    assert(streamer.stream(out_of_bounds, source, DisplayRect{ 0, 0, 2, 1 }, scratch, sizeof(scratch))
           == Status::invalid_argument);
}

}  // namespace

int main()
{
    constexpr auto board = brick::interfaces::board::BoardDescriptor{ "Test board", "STM32F105VCT6", 1u << 0 };
    assert(board.has(brick::interfaces::board::Capability::display));
    using brick::interfaces::display::DisplayCapabilities;
    using brick::interfaces::display::DisplayRect;
    using brick::interfaces::display::PixelBuffer;

    constexpr DisplayRect screen{ 0, 0, 320, 240 };
    assert(screen.contains(DisplayRect{ 10, 20, 100, 50 }));
    assert(!screen.contains(DisplayRect{ 300, 20, 30, 50 }));
    const DisplayRect empty_rect{ 0, 0, 0, 10 };
    assert(empty_rect.empty());

    const std::uint8_t pixels[64] = {};
    const PixelBuffer  partial{ pixels, 8, 4, 20, brick::interfaces::display::PixelFormat::rgb565, true };
    assert(partial.valid());
    assert(partial.stride_bytes > partial.width * 2);

    const DisplayCapabilities capabilities{};
    assert(capabilities.max_buffer_count == 1);

    FakeDisplay       display;
    const PixelBuffer packed{ pixels, 8, 4, 16, brick::interfaces::display::PixelFormat::rgb565, false };
    assert(display.draw_buffer({ 12, 30, 8, 4 }, packed) == Status::ok);
    assert(display.last_area.x == 12 && display.last_area.y == 30);
    assert(display.last_pixels == pixels && display.last_byte_count == 64);
    const PixelBuffer padded{ pixels, 8, 4, 20, brick::interfaces::display::PixelFormat::rgb565, false };
    assert(display.draw_buffer({ 12, 30, 8, 4 }, padded) == Status::invalid_argument);

    test_status();
    test_periodic_timer();
    test_bmp_decoder();
    test_bmp_decoder_wide_rows();
    test_wav_decoder();
    test_audio_player();
    test_screen_canvas();
    test_touch_mapper();
    test_asset_streamer();
    test_asset_bundle();
    test_asset_source_streamer();
    std::puts("BRICK PC tests passed");
    return 0;
}
