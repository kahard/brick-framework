#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "brick/core/image/bmp_decoder.hpp"
#include "brick/core/audio/wav_decoder.hpp"
#include "brick/core/audio/audio_player.hpp"
#include "brick/core/timing/periodic_timer.hpp"

namespace {

class FakeClock final : public brick::interfaces::timing::IClock {
 public:
  std::uint32_t now = 0;
  std::uint32_t millis() const override { return now; }
};

class MemoryFile final : public brick::interfaces::storage::IFile {
 public:
  explicit MemoryFile(const std::vector<std::uint8_t>& data) : data_(data) {}

  std::size_t read(void* buffer, std::size_t size, std::size_t count) override {
    const std::size_t requested = size * count;
    const std::size_t available = data_.size() - position_;
    const std::size_t bytes = requested < available ? requested : available;
    if (bytes > 0) {
      std::memcpy(buffer, data_.data() + position_, bytes);
      position_ += bytes;
    }
    return size == 0 ? 0 : bytes / size;
  }

  bool seek(long offset, int origin) override {
    long base = 0;
    if (origin == SEEK_CUR) base = static_cast<long>(position_);
    if (origin == SEEK_END) base = static_cast<long>(data_.size());
    const long target = base + offset;
    if (target < 0 || target > static_cast<long>(data_.size())) return false;
    position_ = static_cast<std::size_t>(target);
    return true;
  }

 private:
  const std::vector<std::uint8_t>& data_;
  std::size_t position_ = 0;
};

class MemoryFileSystem final : public brick::interfaces::storage::IFileSystem {
 public:
  bool mount() override { return true; }

  std::vector<std::string> list_files(const char*) override { return {}; }

  std::unique_ptr<brick::interfaces::storage::IFile> open(
      const char* path, const char*) override {
    const auto it = files.find(path);
    if (it == files.end()) return nullptr;
    return std::make_unique<MemoryFile>(it->second);
  }

  std::map<std::string, std::vector<std::uint8_t>> files;
};

class FakeAudioOutput final : public brick::interfaces::audio::IAudioOutput {
 public:
  bool begin() override { started = true; return true; }
  void write_sample(std::uint8_t sample) override { samples.push_back(sample); }
  void stop() override { stopped = true; }

  bool started = false;
  bool stopped = false;
  std::vector<std::uint8_t> samples;
};

void test_periodic_timer() {
  FakeClock clock;
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

std::vector<std::uint8_t> make_2x2_bmp() {
  // 24-bit, bottom-up BMP. Rows are padded to four-byte boundaries.
  std::vector<std::uint8_t> bmp(70, 0);
  bmp[0] = 'B'; bmp[1] = 'M';
  bmp[2] = 70;
  bmp[10] = 54;
  bmp[14] = 40;
  bmp[18] = 2;
  bmp[22] = 2;
  bmp[26] = 1;
  bmp[28] = 24;

  // Bottom row: blue, white.
  bmp[54] = 255; bmp[55] = 0;   bmp[56] = 0;
  bmp[57] = 255; bmp[58] = 255; bmp[59] = 255;
  // Top row: red, green.
  bmp[62] = 0;   bmp[63] = 0;   bmp[64] = 255;
  bmp[65] = 0;   bmp[66] = 255; bmp[67] = 0;
  return bmp;
}

void test_bmp_decoder() {
  MemoryFileSystem fs;
  fs.files["test.bmp"] = make_2x2_bmp();
  std::uint8_t pixels[2 * 2 * 2] = {};
  const bool decoded = brick::core::image::BmpDecoder::decode(
      fs, "test.bmp", pixels, {.target_width = 2, .target_height = 2});
  assert(decoded);

  // RGB565, native little endian: top-left red, top-right green,
  // bottom-left blue, white.
  const std::uint8_t expected[] = {
      0x00, 0xF8, 0xE0, 0x07,
      0x1F, 0x00, 0xFF, 0xFF,
  };
  assert(std::memcmp(pixels, expected, sizeof(expected)) == 0);
  assert(!brick::core::image::BmpDecoder::decode(
      fs, "missing.bmp", pixels, {.target_width = 2, .target_height = 2}));
}

std::vector<std::uint8_t> make_pcm_wav() {
  std::vector<std::uint8_t> wav(46, 0);
  std::memcpy(wav.data(), "RIFF", 4);
  wav[4] = 38;
  std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
  wav[16] = 16;
  wav[20] = 1;  // PCM
  wav[22] = 1;  // mono
  wav[24] = 0x40; wav[25] = 0x1F;  // 8000 Hz
  wav[28] = 0x40; wav[29] = 0x1F;  // byte rate
  wav[32] = 1;  // block alignment
  wav[34] = 8;  // bits per sample
  std::memcpy(wav.data() + 36, "data", 4);
  wav[40] = 2;
  wav[44] = 0;
  wav[45] = 255;
  return wav;
}

void test_wav_decoder() {
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

void test_audio_player() {
  FakeAudioOutput output;
  brick::core::audio::PeriodicAudioPlayer player(output);
  brick::core::audio::AudioBuffer buffer({10, 20, 30}, 8000);
  assert(player.begin());
  assert(player.play(buffer));
  assert(player.playing());
  player.tick();
  player.tick();
  player.tick();
  assert(!player.playing());
  assert(output.samples == std::vector<std::uint8_t>({10, 20, 30, 128}));
  assert(output.stopped);
}

}  // namespace

int main() {
  test_periodic_timer();
  test_bmp_decoder();
  test_wav_decoder();
  test_audio_player();
  std::puts("BRICK PC tests passed");
  return 0;
}
