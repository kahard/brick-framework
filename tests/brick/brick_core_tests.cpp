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

#include "brick/core/audio/BeepSequence.h"
#include "brick/core/audio/PeriodicAudioPlayer.h"
#include "brick/core/audio/WavDecoder.h"
#include "brick/core/display/FrameBufferCopy.h"
#include "brick/core/display/Rgb565Pixels.h"
#include "brick/core/display/Screen.h"
#include "brick/core/image/BmpDecoder.h"
#include "brick/core/input/DragSlider.h"
#include "brick/core/input/TouchMapper.h"
#include "brick/core/logging/SerialLogger.h"
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

std::uint16_t pixel_at(const std::uint8_t* pixels, int width, int x, int y)
{
    const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 2;
    return static_cast<std::uint16_t>(pixels[index] | (pixels[index + 1] << 8));
}

int g_reported_rows = 0;

void count_row(std::uint32_t)
{
    ++g_reported_rows;
}

void test_bmp_decoder_placement()
{
    MemoryFileSystem fs;
    fs.files["small.bmp"] = make_2x2_bmp();
    fs.files["tall.bmp"]  = make_gradient_bmp(3, 5, 24);

    // A smaller image is centred and the border is cleared.
    std::uint8_t centred[4 * 4 * 2];
    std::memset(centred, 0xAA, sizeof(centred));
    assert(brick::core::image::BmpDecoder::decode(fs, "small.bmp", centred, { .target_width = 4, .target_height = 4 })
           == Status::ok);
    assert(pixel_at(centred, 4, 1, 1) == 0xF800 && pixel_at(centred, 4, 2, 1) == 0x07E0);
    assert(pixel_at(centred, 4, 1, 2) == 0x001F && pixel_at(centred, 4, 2, 2) == 0xFFFF);
    assert(pixel_at(centred, 4, 0, 0) == 0 && pixel_at(centred, 4, 3, 3) == 0 && pixel_at(centred, 4, 0, 2) == 0);

    // A taller image keeps its middle rows; on_row reports progress.
    std::uint8_t cropped[3 * 3 * 2];
    g_reported_rows = 0;
    assert(brick::core::image::BmpDecoder::decode(fs, "tall.bmp", cropped,
                                                  { .target_width = 3, .target_height = 3, .on_row = count_row })
           == Status::ok);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            assert(pixel_at(cropped, 3, x, y) == expected_gradient(x, y + 1));
    assert(g_reported_rows == 1);

    std::vector<std::uint8_t> rgb555  = make_2x2_bmp();
    rgb555[28]                        = 16;
    std::vector<std::uint8_t> not_bmp = make_2x2_bmp();
    not_bmp[0]                        = 'X';
    fs.files["rgb555.bmp"]            = rgb555;
    fs.files["not_bmp.bmp"]           = not_bmp;
    assert(brick::core::image::BmpDecoder::decode(fs, "rgb555.bmp", centred, { .target_width = 4, .target_height = 4 })
           == Status::not_supported);
    assert(brick::core::image::BmpDecoder::decode(fs, "not_bmp.bmp", centred, { .target_width = 4, .target_height = 4 })
           == Status::error);
    assert(brick::core::image::BmpDecoder::decode(fs, "small.bmp", centred, { .target_width = 0, .target_height = 4 })
           == Status::invalid_argument);

    // Corrupted dimensions would overflow the row offsets.
    std::vector<std::uint8_t> huge_width  = make_2x2_bmp();
    huge_width[20]                        = 1;  // 65538 pixels
    std::vector<std::uint8_t> huge_height = make_2x2_bmp();
    huge_height[22] = huge_height[23] = huge_height[24] = 0;
    huge_height[25]                                     = 0x80;  // INT32_MIN
    fs.files["huge_width.bmp"]                          = huge_width;
    fs.files["huge_height.bmp"]                         = huge_height;
    assert(
        brick::core::image::BmpDecoder::decode(fs, "huge_width.bmp", centred, { .target_width = 4, .target_height = 4 })
        == Status::not_supported);
    assert(brick::core::image::BmpDecoder::decode(fs, "huge_height.bmp", centred,
                                                  { .target_width = 4, .target_height = 4 })
           == Status::not_supported);
    assert(fs.open_files() == 0);
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

// A WAV whose extra_chunks variant has an odd-sized LIST chunk (with its pad
// byte) before "fmt " and an 18-byte fmt chunk, as some editors write them.
std::vector<std::uint8_t> make_wav(std::uint16_t format, std::uint16_t channels, std::uint16_t bits,
                                   const std::vector<std::uint8_t>& data, bool extra_chunks)
{
    std::vector<std::uint8_t> wav;
    const auto                tag = [&wav](const char* text) { wav.insert(wav.end(), text, text + 4); };
    const auto                u16 = [&wav](unsigned value)
    {
        wav.push_back(static_cast<std::uint8_t>(value & 0xFF));
        wav.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    };
    const auto u32 = [&u16](std::uint32_t value)
    {
        u16(value & 0xFFFF);
        u16(value >> 16);
    };
    const unsigned frame_bytes = channels * bits / 8U;
    tag("RIFF");
    u32(0);
    tag("WAVE");
    if (extra_chunks)
    {
        tag("LIST");
        u32(3);
        wav.insert(wav.end(), { 1, 2, 3, 0 });
    }
    tag("fmt ");
    u32(extra_chunks ? 18 : 16);
    u16(format);
    u16(channels);
    u32(8000);
    u32(8000 * frame_bytes);
    u16(frame_bytes);
    u16(bits);
    if (extra_chunks)
        u16(0);
    tag("data");
    u32(static_cast<std::uint32_t>(data.size()));
    wav.insert(wav.end(), data.begin(), data.end());
    return wav;
}

void test_wav_decoder_formats()
{
    // 16-bit stereo frames: full scale, negative full scale, silence.
    const std::vector<std::uint8_t> stereo = { 0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x80, 0x00, 0x80, 0x00, 0x10, 0x00, 0xF0 };
    for (const bool extra_chunks : { false, true })
    {
        MemoryFileSystem fs;
        fs.files["stereo.wav"]                     = make_wav(1, 2, 16, stereo, extra_chunks);
        std::uint8_t                    samples[3] = {};
        brick::core::audio::AudioBuffer buffer;
        assert(brick::core::audio::WavDecoder::decode(fs, "stereo.wav", samples, sizeof(samples), buffer)
               == Status::ok);
        assert(buffer.size() == 3 && buffer.sample_rate() == 8000);
        assert(samples[0] == 255 && samples[1] == 0 && samples[2] == 128);
        assert(fs.open_files() == 0);
    }

    MemoryFileSystem fs;
    fs.files["float.wav"]                      = make_wav(3, 1, 16, { 0, 0 }, false);
    fs.files["24bit.wav"]                      = make_wav(1, 1, 24, { 0, 0, 0 }, false);
    fs.files["empty.wav"]                      = make_wav(1, 1, 8, {}, false);
    fs.files["truncated.wav"]                  = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A' };
    std::uint8_t                    samples[4] = {};
    brick::core::audio::AudioBuffer buffer;
    assert(brick::core::audio::WavDecoder::decode(fs, "float.wav", samples, sizeof(samples), buffer)
           == Status::not_supported);
    assert(brick::core::audio::WavDecoder::decode(fs, "24bit.wav", samples, sizeof(samples), buffer)
           == Status::not_supported);
    assert(brick::core::audio::WavDecoder::decode(fs, "empty.wav", samples, sizeof(samples), buffer) == Status::error);
    assert(brick::core::audio::WavDecoder::decode(fs, "truncated.wav", samples, sizeof(samples), buffer)
           == Status::error);
    assert(brick::core::audio::WavDecoder::decode(fs, "float.wav", nullptr, sizeof(samples), buffer)
           == Status::invalid_argument);
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

    // fill_area() draws a 16x10 area through 64 pixels: strips of 4, 4 and 2 rows.
    assert(screen.fill_area({ 2, 20, 16, 10 }, 0x07E0, pixels) == Status::ok);
    assert(pixels[0] == 0x07E0 && display.last_area.y == 28 && display.last_area.height == 2);
    assert(screen.fill_area({ 0, 0, 65, 1 }, 0, pixels) == Status::invalid_argument);
}

void test_frame_buffer_copy()
{
    using brick::core::display::copy_frame_area;
    using brick::core::display::copy_into_frame;
    using brick::interfaces::display::PixelBuffer;
    using brick::interfaces::display::PixelFormat;
    using brick::interfaces::display::WritablePixelBuffer;

    constexpr std::uint32_t                     kWidth  = 8;
    constexpr std::uint32_t                     kHeight = 6;
    std::array<std::uint16_t, kWidth * kHeight> first{};
    std::array<std::uint16_t, kWidth * kHeight> second{};
    const WritablePixelBuffer                   first_frame{
        reinterpret_cast<std::uint8_t*>(first.data()), kWidth, kHeight, kWidth * 2, PixelFormat::rgb565, false
    };
    const WritablePixelBuffer second_frame{
        reinterpret_cast<std::uint8_t*>(second.data()), kWidth, kHeight, kWidth * 2, PixelFormat::rgb565, false
    };

    // A 3x2 area at (2,1) from pixels with a padded stride.
    const std::array<std::uint16_t, 8> pixels{ 1, 2, 3, 0, 4, 5, 6, 0 };
    const PixelBuffer                  area_pixels{
        reinterpret_cast<const std::uint8_t*>(pixels.data()), 3, 2, 8, PixelFormat::rgb565, false
    };
    assert(copy_into_frame(first_frame, { 2, 1, 3, 2 }, area_pixels) == brick::Status::ok);
    assert(first[1 * kWidth + 2] == 1 && first[1 * kWidth + 4] == 3 && first[2 * kWidth + 3] == 5);
    assert(first[1 * kWidth + 1] == 0 && first[1 * kWidth + 5] == 0 && first[3 * kWidth + 2] == 0);

    // The same area into the other frame, nothing around it.
    assert(copy_frame_area(first_frame, second_frame, { 2, 1, 3, 2 }) == brick::Status::ok);
    assert(second == first);

    // Outside the frame, empty, or the wrong size: refused, nothing written.
    assert(copy_into_frame(first_frame, { 6, 0, 3, 2 }, area_pixels) == brick::Status::invalid_argument);
    assert(copy_into_frame(first_frame, { 0, 0, 2, 2 }, area_pixels) == brick::Status::invalid_argument);
    assert(copy_frame_area(first_frame, second_frame, {}) == brick::Status::invalid_argument);
    assert(second == first);
}

void test_rgb565_pixels()
{
    using brick::core::display::copy_rgb565_swapped;
    using brick::core::display::fill_rgb565;

    // Every start alignment and length, also odd ones, against a plain loop.
    alignas(8) std::array<std::uint16_t, 40> pixels{};
    for (std::size_t start = 0; start < 4; ++start)
        for (std::size_t count = 0; count < 20; ++count)
        {
            pixels.fill(0x1111);
            fill_rgb565(pixels.data() + start, count, 0xABCD);
            for (std::size_t index = 0; index < pixels.size(); ++index)
                assert(pixels[index] == (index >= start && index < start + count ? 0xABCD : 0x1111));
        }

    alignas(8) std::array<std::uint8_t, 48> source{};
    for (std::size_t index = 0; index < source.size(); ++index)
        source[index] = static_cast<std::uint8_t>(index * 7 + 3);
    alignas(8) std::array<std::uint8_t, 48> destination{};
    for (std::size_t source_offset = 0; source_offset < 4; ++source_offset)
        for (std::size_t destination_offset = 0; destination_offset < 4; ++destination_offset)
            for (std::size_t bytes = 0; bytes <= 40; bytes += 2)
            {
                destination.fill(0xEE);
                copy_rgb565_swapped(destination.data() + destination_offset, source.data() + source_offset, bytes);
                for (std::size_t index = 0; index < destination.size(); ++index)
                {
                    const bool        inside   = index >= destination_offset && index < destination_offset + bytes;
                    const std::size_t relative = index - destination_offset;
                    const auto        expected = inside ? source[source_offset + (relative ^ 1U)] : 0xEE;
                    assert(destination[index] == expected);
                }
            }
}

/// Glyph type of the generated bitmap fonts: rows of MSB-first bits.
struct TestGlyph
{
    std::uint16_t       width;
    std::uint16_t       height;
    std::int16_t        top;
    std::uint16_t       stride;
    const std::uint8_t* data;
};

/// The straightforward per-pixel renderer draw_text() must match.
void reference_text(std::vector<std::uint16_t>& pixels, std::int32_t width, std::int32_t height, std::int32_t x,
                    std::int32_t y, const char* text, const char* characters, const TestGlyph* glyphs,
                    std::size_t count, std::uint16_t color)
{
    for (const char* cursor = text; *cursor != '\0'; ++cursor)
    {
        const char* found = std::strchr(characters, *cursor);
        if (found == nullptr)
            continue;
        const TestGlyph& glyph = glyphs[static_cast<std::size_t>(found - characters)];
        (void)count;
        for (std::int32_t row = 0; row < glyph.height; ++row)
            for (std::int32_t column = 0; column < glyph.width; ++column)
            {
                const std::int32_t px = x + column;
                const std::int32_t py = y + glyph.top + row;
                if ((glyph.data[row * glyph.stride + column / 8] & (0x80U >> (column & 7))) != 0 && px >= 0 && py >= 0
                    && px < width && py < height)
                    pixels[static_cast<std::size_t>(py * width + px)] = color;
            }
        x += glyph.width + 1;
        if (x >= width)
            break;
    }
}

void test_screen_fill_and_text()
{
    FakeDisplay                  display;
    brick::core::display::Screen screen(display);

    // fill() keeps the stride padding of every row.
    std::vector<std::uint16_t>                      padded(7 * 3, 0x5555);
    brick::interfaces::display::WritablePixelBuffer view{
        reinterpret_cast<std::uint8_t*>(padded.data()),  5,    3, 7 * 2,
        brick::interfaces::display::PixelFormat::rgb565, false
    };
    assert(screen.fill(view, 0x1234) == Status::ok);
    for (std::size_t index = 0; index < padded.size(); ++index)
        assert(padded[index] == (index % 7 < 5 ? 0x1234 : 0x5555));

    // A 10-pixel-wide glyph spans two bitmap bytes, one of them empty in
    // some rows; another glyph sits above its baseline (negative top).
    static constexpr std::uint8_t wide_bits[]  = { 0xFF, 0xC0, 0x00, 0x40, 0x81, 0x00, 0x00, 0x00, 0xAA, 0x80 };
    static constexpr std::uint8_t small_bits[] = { 0xE0, 0xA0, 0xE0 };
    const TestGlyph               glyphs[]     = { { 10, 5, 1, 2, wide_bits }, { 3, 3, -2, 1, small_bits } };
    const char                    characters[] = "AB";
    constexpr std::int32_t        kWidth       = 23;
    constexpr std::int32_t        kHeight      = 9;
    for (std::int32_t x = -12; x <= kWidth; x += 3)
        for (std::int32_t y = -6; y <= kHeight; ++y)
        {
            std::vector<std::uint16_t> expected(kWidth * kHeight, 0);
            std::vector<std::uint16_t> actual(kWidth * kHeight, 0);
            reference_text(expected, kWidth, kHeight, x, y, "AxBA", characters, glyphs, 2, 0xF00D);
            brick::interfaces::display::WritablePixelBuffer target{
                reinterpret_cast<std::uint8_t*>(actual.data()),  kWidth, kHeight, kWidth * 2,
                brick::interfaces::display::PixelFormat::rgb565, false
            };
            assert(screen.draw_text(target, x, y, "AxBA", characters, glyphs, 2, 0xF00D) == Status::ok);
            assert(actual == expected);
        }
}

void test_screen_hardware_fill()
{
    FakeDisplay display;
    display.hardware_fill = true;
    brick::core::display::Screen   screen(display);
    std::array<std::uint16_t, 128> pixels{};

    // The device fills the whole area at once; the caller's pixels stay unused.
    assert(screen.fill_area({ 10, 20, 100, 50 }, 0x07E0, pixels) == Status::ok);
    assert(display.filled_areas.size() == 1 && display.filled_areas[0].width == 100);
    assert(display.last_fill_color == 0x07E0 && display.submitted_areas.empty() && pixels[0] == 0);
    // The same preconditions as without hardware fill: a row must fit the pixels.
    assert(screen.fill_area({ 0, 0, 129, 1 }, 0, pixels) == Status::invalid_argument);
    assert(screen.fill_area({ 300, 0, 30, 1 }, 0, pixels) == Status::invalid_argument);
    assert(display.filled_areas.size() == 1);
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

brick::interfaces::display::TouchPoint touch_at(std::int16_t x, brick::interfaces::display::TouchState state)
{
    brick::interfaces::display::TouchPoint point{};
    point.x     = x;
    point.state = state;
    return point;
}

void test_beep_sequence()
{
    brick::mocks::FakeBuzzer         buzzer;
    brick::core::audio::BeepSequence beeps(buzzer, 80, 120);
    assert(beeps.start(2, 1000) == Status::ok && buzzer.enabled && beeps.active());
    assert(beeps.update(1079) == Status::ok && buzzer.enabled);
    assert(beeps.update(1080) == Status::ok && !buzzer.enabled);
    assert(beeps.update(1199) == Status::ok && !buzzer.enabled);
    assert(beeps.update(1200) == Status::ok && buzzer.enabled);
    assert(beeps.update(1280) == Status::ok && !buzzer.enabled && !beeps.active());
    assert(beeps.update(5000) == Status::ok && buzzer.switches.size() == 4);

    // Timing across the wrap of the millisecond counter.
    assert(beeps.start(1, 0xFFFFFFF0U) == Status::ok && buzzer.enabled);
    assert(beeps.update(0x10U) == Status::ok && buzzer.enabled);
    assert(beeps.update(0x40U) == Status::ok && !buzzer.enabled && !beeps.active());

    // Two beeps repeated after a 1 s pause: on 0-80, 200-280, again from 1280.
    assert(beeps.start(2, 0, 1000) == Status::ok && buzzer.enabled);
    for (const std::uint32_t now : { 80U, 200U, 280U })
        assert(beeps.update(now) == Status::ok);
    assert(!buzzer.enabled && beeps.active());
    assert(beeps.update(1279) == Status::ok && !buzzer.enabled);
    assert(beeps.update(1280) == Status::ok && buzzer.enabled);

    // A failing buzzer ends the sequence.
    buzzer.result = Status::not_ready;
    assert(beeps.start(3, 0) == Status::not_ready && !beeps.active());
}

void log_line(brick::core::logging::SerialLogger& logger, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    logger.write(brick::interfaces::logging::Level::warning, "app", format, args);
    va_end(args);
}

void test_serial_logger()
{
    brick::mocks::FakeSerialPort       port;
    brick::core::logging::SerialLogger logger(port);
    logger.info("board", "started %d of %d", 3, 4);
    assert(port.written == "I (board) started 3 of 4\r\n");

    port.written.clear();
    logger.debug("board", "hidden");
    assert(port.written.empty());

    // A long line is cut but still ends the line.
    port.written.clear();
    log_line(logger, "%0200d", 7);
    assert(port.written.size() == brick::core::logging::SerialLogger::kLineBytes);
    assert(port.written.compare(0, 9, "W (app) 0") == 0);
    assert(port.written.compare(port.written.size() - 2, 2, "\r\n") == 0);
}

void test_drag_slider()
{
    using brick::interfaces::display::TouchState;
    brick::core::input::DragSlider slider(319, 400);
    assert(slider.position() == 160);

    // A drag moves the slider by the finger's movement, clamped to 0..319.
    assert(!slider.update(touch_at(100, TouchState::pressed), 1000));
    assert(slider.update(touch_at(150, TouchState::moved), 1010) && slider.position() == 210);
    assert(!slider.update(touch_at(150, TouchState::moved), 1020));
    assert(slider.update(touch_at(400, TouchState::moved), 1030) && slider.position() == 319);
    assert(!slider.update(touch_at(400, TouchState::released), 1040));
    // A move without a press first only starts the drag.
    assert(!slider.update(touch_at(10, TouchState::moved), 1050));
    assert(slider.update(touch_at(-5, TouchState::moved), 1060) && slider.position() == 309);
    assert(!slider.update(touch_at(0, TouchState::released), 1070));

    // Taps 300 ms apart centre it; a third tap does not count as another double tap.
    assert(!slider.update(touch_at(20, TouchState::pressed), 5000));
    assert(slider.update(touch_at(20, TouchState::pressed), 5300) && slider.position() == 160);
    assert(slider.update(touch_at(30, TouchState::moved), 5310) && slider.position() == 170);
    assert(!slider.update(touch_at(30, TouchState::pressed), 5400) && slider.position() == 170);
    // Taps 500 ms apart do not.
    assert(!slider.update(touch_at(30, TouchState::pressed), 5900) && slider.position() == 170);

    // A double tap across the millisecond counter wrap, the first tap at 0.
    brick::core::input::DragSlider wrapping(99, 400);
    assert(!wrapping.update(touch_at(0, TouchState::pressed), 0));
    assert(wrapping.update(touch_at(9, TouchState::moved), 10) && wrapping.position() == 59);
    assert(!wrapping.update(touch_at(9, TouchState::pressed), 0xFFFFFF00U));
    assert(wrapping.update(touch_at(9, TouchState::pressed), 0x40U) && wrapping.position() == 50);
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

/// Display with queued transfers: a submitted buffer stays in use until
/// wait_for_transfer_complete(), and it records each stripe's pixels.
class AsyncDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    Status                                  begin() override { return Status::ok; }
    brick::interfaces::display::DisplaySize size() const override { return { 320, 240 }; }
    brick::interfaces::display::PixelFormat pixel_format() const override
    {
        return brick::interfaces::display::PixelFormat::rgb565;
    }
    Status set_rotation(brick::interfaces::display::Rotation) override { return Status::ok; }
    brick::interfaces::display::DisplayCapabilities capabilities() const override
    {
        brick::interfaces::display::DisplayCapabilities result{};
        result.async_transfer = true;
        return result;
    }
    Status draw_buffer(brick::interfaces::display::DisplayRect, const brick::interfaces::display::PixelBuffer&) override
    {
        return Status::not_supported;
    }
    Status submit_buffer(brick::interfaces::display::DisplayRect        area,
                         const brick::interfaces::display::PixelBuffer& buffer) override
    {
        if (in_flight != nullptr)
            return Status::busy;
        in_flight       = buffer.data;
        in_flight_bytes = buffer.stride_bytes * buffer.height;
        received.insert(received.end(), buffer.data, buffer.data + in_flight_bytes);
        areas.push_back(area);
        return Status::ok;
    }
    Status wait_for_transfer_complete(std::uint32_t) override
    {
        in_flight = nullptr;
        ++waits;
        return Status::ok;
    }

    const std::uint8_t*                                  in_flight       = nullptr;
    std::size_t                                          in_flight_bytes = 0;
    std::vector<std::uint8_t>                            received;
    std::vector<brick::interfaces::display::DisplayRect> areas;
    int                                                  waits = 0;
};

/// Reader that fails the test when it writes into a buffer still in flight.
class CheckingReader final : public brick::interfaces::display::IAssetReader
{
public:
    CheckingReader(const std::vector<std::uint8_t>& data, const AsyncDisplay& display) : data_(data), display_(display)
    {
    }

    Status read(const brick::interfaces::display::ImageAsset&, std::size_t offset, std::uint8_t* destination,
                std::size_t bytes) override
    {
        const std::uint8_t* busy = display_.in_flight;
        assert(busy == nullptr || destination + bytes <= busy || destination >= busy + display_.in_flight_bytes);
        if (fail_at_offset == offset)
            return Status::error;
        std::memcpy(destination, data_.data() + offset, bytes);
        if (busy != nullptr)
            ++overlapped_reads;
        return Status::ok;
    }

    std::size_t fail_at_offset   = SIZE_MAX;
    int         overlapped_reads = 0;

private:
    const std::vector<std::uint8_t>& data_;
    const AsyncDisplay&              display_;
};

void test_asset_streamer_pipelined()
{
    using brick::interfaces::display::DisplayRect;
    using brick::interfaces::display::ImageAsset;
    using brick::interfaces::display::PixelFormat;

    std::vector<std::uint8_t> pixels(4 * 2 * 7);
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<std::uint8_t>(i + 1);
    const ImageAsset asset{ pixels.data(), 4, 7, 8, pixels.size(), PixelFormat::rgb565 };

    // 32 bytes of scratch: two halves of two rows each; 7 rows in 4 stripes.
    AsyncDisplay                      display;
    CheckingReader                    reader(pixels, display);
    brick::core::image::AssetStreamer streamer(display, reader);
    std::uint8_t                      scratch[32] = {};
    assert(streamer.stream(asset, DisplayRect{ 0, 10, 4, 7 }, scratch, sizeof(scratch)) == Status::ok);
    assert(display.received == pixels);
    assert(display.areas.size() == 4 && display.areas[3].y == 16 && display.areas[3].height == 1);
    // Every stripe after the first was read while the previous one was sent,
    // and nothing is in flight when stream() returns.
    assert(reader.overlapped_reads == 3 && display.in_flight == nullptr && display.waits == 4);

    // A failed read still waits for the stripe in flight before returning.
    AsyncDisplay                      failing_display;
    CheckingReader                    failing_reader(pixels, failing_display);
    brick::core::image::AssetStreamer failing(failing_display, failing_reader);
    failing_reader.fail_at_offset = 16;
    assert(failing.stream(asset, DisplayRect{ 0, 0, 4, 7 }, scratch, sizeof(scratch)) == Status::error);
    assert(failing_display.in_flight == nullptr && failing_display.areas.size() == 1);

    // Scratch for a single stripe: no overlap, every stripe is waited for.
    AsyncDisplay                      single_display;
    CheckingReader                    single_reader(pixels, single_display);
    brick::core::image::AssetStreamer single(single_display, single_reader);
    assert(single.stream(asset, DisplayRect{ 0, 0, 4, 7 }, scratch, 15) == Status::ok);
    assert(single_reader.overlapped_reads == 0 && single_display.received == pixels);
}

void test_asset_streamer_padded_rows()
{
    using brick::interfaces::display::ImageAsset;
    using brick::interfaces::display::PixelFormat;

    // Rows of 4 bytes (2 pixels) stored 6 bytes apart; the last row has no padding.
    const std::vector<std::uint8_t>   padded{ 1, 2, 3, 4, 0, 0, 5, 6, 7, 8, 0, 0, 9, 10, 11, 12 };
    const ImageAsset                  asset{ padded.data(), 2, 3, 6, padded.size(), PixelFormat::rgb565 };
    FakeDisplay                       display;
    MemoryAssetReader                 reader(padded);
    brick::core::image::AssetStreamer streamer(display, reader);
    std::uint8_t                      scratch[8] = {};
    std::uint8_t                      out[12]    = {};
    const brick::interfaces::display::WritablePixelBuffer packed{ out, 2, 3, 4, PixelFormat::rgb565, false };
    assert(streamer.stream_to_buffer(asset, packed, scratch, sizeof(scratch)) == Status::ok);
    const std::uint8_t expected[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    assert(std::memcmp(out, expected, sizeof(expected)) == 0);
    // Packed destination rows are read in place: scratch is not touched.
    assert(scratch[0] == 0 && scratch[7] == 0);
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
    test_bmp_decoder_placement();
    test_wav_decoder();
    test_wav_decoder_formats();
    test_audio_player();
    test_screen_canvas();
    test_rgb565_pixels();
    test_frame_buffer_copy();
    test_screen_fill_and_text();
    test_screen_hardware_fill();
    test_touch_mapper();
    test_drag_slider();
    test_beep_sequence();
    test_serial_logger();
    test_asset_streamer();
    test_asset_streamer_pipelined();
    test_asset_streamer_padded_rows();
    test_asset_bundle();
    test_asset_source_streamer();
    std::puts("BRICK PC tests passed");
    return 0;
}
