#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "brick/boards/pc/SimulatorBoard.h"
#include "brick/core/memory/MemoryBlock.h"
#include "brick/core/storage/StorageWriteVerify.h"
#include "brick/platform/pc/PcFileAssetSource.h"
#include "brick/platform/pc/PcFileSystem.h"
#include "brick/platform/pc/PcFrameBufferDisplay.h"
#include "brick/platform/pc/PcInput.h"
#include "brick/platform/pc/PcMemory.h"
#include "brick/platform/pc/PcSerialPort.h"
#include "brick/platform/pc/PcTime.h"
#include "brick/platform/pc/Simulator.h"

namespace
{

using brick::Status;
using brick::interfaces::display::DisplayRect;
using brick::interfaces::display::PixelBuffer;
using brick::interfaces::display::Rotation;
using brick::interfaces::display::TouchPoint;
using brick::interfaces::display::TouchState;
using brick::interfaces::display::WritablePixelBuffer;
using brick::platform::pc::PcFrameBufferDisplay;

void test_partial_display()
{
    PcFrameBufferDisplay display({ 4, 2, 0 });
    const std::uint16_t  early[1] = { 0 };
    assert(display.draw_buffer({ 0, 0, 1, 1 }, PixelBuffer{ reinterpret_cast<const std::uint8_t*>(early), 1, 1, 2 })
           == Status::not_ready);
    assert(display.begin() == Status::ok);
    assert(display.wait_for_vsync(10) == Status::not_supported);
    assert(display.size().width == 4 && display.size().height == 2);
    assert(!display.capabilities().scanout_buffers && display.capabilities().dma);
    assert(display.frame_buffer_count() == 0);

    const std::uint16_t pixels[2] = { 0xF800, 0x07E0 };
    const PixelBuffer   buffer{ reinterpret_cast<const std::uint8_t*>(pixels), 2, 1, 4 };
    assert(display.draw_buffer(DisplayRect{ 1, 1, 2, 1 }, buffer) == Status::ok);
    assert(display.screen()[4 + 1] == 0xF800 && display.screen()[4 + 2] == 0x07E0);
    assert(display.frame_counter() == 1);
    // Outside the screen.
    assert(display.draw_buffer(DisplayRect{ 3, 1, 2, 1 }, buffer) == Status::invalid_argument);
    assert(display.draw_buffer(DisplayRect{ -1, 0, 2, 1 }, buffer) == Status::invalid_argument);

    assert(display.set_rotation(Rotation::rotate_90) == Status::ok);
    assert(display.size().width == 2 && display.size().height == 4);
}

void test_frame_buffer_display()
{
    PcFrameBufferDisplay display({ 3, 2, 2 });
    assert(display.begin() == Status::ok);
    assert(display.capabilities().scanout_buffers && display.capabilities().dma);
    assert(display.wait_for_vsync(10) == Status::ok);
    WritablePixelBuffer back{};
    assert(display.frame_buffer_count() == 2 && display.get_frame_buffer(1, back) == Status::ok);
    assert(back.width == 3 && back.height == 2 && back.stride_bytes == 6);
    reinterpret_cast<std::uint16_t*>(back.data)[5] = 0x1234;
    assert(display.present_frame_buffer(1) == Status::ok);
    assert(display.screen()[5] == 0x1234);
    assert(display.get_frame_buffer(2, back) == Status::invalid_argument);
}

void test_touch_and_button()
{
    brick::platform::pc::PcTouchscreen touch;
    TouchPoint                         points[2];
    std::size_t                        count = 9;
    assert(touch.read(points, 2, count) == Status::no_data && count == 0);
    touch.set(true, 10, 20);
    assert(touch.read(points, 2, count) == Status::ok && count == 1 && points[0].state == TouchState::pressed);
    assert(points[0].x == 10 && points[0].y == 20);
    assert(touch.read(points, 2, count) == Status::no_data);  // no new sample
    touch.set(true, 11, 21);
    assert(touch.read(points, 2, count) == Status::ok && count == 1 && points[0].state == TouchState::moved);
    // Like the hardware drivers: one released sample at the last position.
    touch.set(false, 0, 0);
    assert(touch.read(points, 2, count) == Status::ok && count == 1 && points[0].state == TouchState::released);
    assert(points[0].x == 11 && points[0].y == 21 && !touch.pressed());

    brick::platform::pc::PcButton button;
    assert(button.begin() == Status::ok && !button.is_pressed());
    button.set_pressed(true);
    assert(button.is_pressed());
}

void test_file_system()
{
    const auto root = std::filesystem::path("pc_fs_test");
    std::filesystem::remove_all(root);
    using brick::interfaces::storage::FileEntry;
    using brick::interfaces::storage::FileHandle;
    brick::platform::pc::PcFileSystem fs(root.string());
    FileHandle                        early;
    assert(fs.open("/sdcard/data.bin", "w", early) == Status::not_ready);
    assert(fs.mount() == Status::not_found);
    std::filesystem::create_directories(root);
    assert(fs.mount() == Status::ok);

    {
        FileHandle file;
        assert(fs.open("/sdcard/data.bin", "w", file) == Status::ok && file.is_open());
        const char text[] = "brick";
        assert(file->write(text, 1, 5) == 5);
    }
    FileHandle file;
    assert(fs.open("/sdcard/data.bin", "r", file) == Status::ok);
    char read_back[6] = {};
    assert(file->read(read_back, 1, 5) == 5 && std::strcmp(read_back, "brick") == 0);
    assert(file->seek(1, SEEK_SET) == Status::ok && file->read(read_back, 1, 1) == 1 && read_back[0] == 'r');
    FileHandle missing;
    assert(fs.open("/sdcard/missing.bin", "r", missing) == Status::not_found && !missing);

    // Directory listing: names without the directory, total count returned.
    FileHandle other;
    assert(fs.open("/sdcard/other.bin", "w", other) == Status::ok);
    other.close();
    FileEntry entries[1];
    assert(fs.list_files("/sdcard", nullptr, 0) == 2);
    assert(fs.list_files("/sdcard", entries, 1) == 2 && std::strcmp(entries[0].name, "data.bin") == 0);

    // The file objects are a fixed set; closing a handle frees its object.
    FileHandle extra[brick::platform::pc::PcFileSystem::kMaxOpenFiles];
    for (std::size_t index = 0; index + 1 < brick::platform::pc::PcFileSystem::kMaxOpenFiles; ++index)
        assert(fs.open("/sdcard/data.bin", "r", extra[index]) == Status::ok);
    assert(fs.open("/sdcard/data.bin", "r", extra[brick::platform::pc::PcFileSystem::kMaxOpenFiles - 1])
           == Status::busy);
    extra[0].close();
    assert(fs.open("/sdcard/data.bin", "r", extra[brick::platform::pc::PcFileSystem::kMaxOpenFiles - 1]) == Status::ok);
    for (FileHandle& handle : extra)
        handle.close();

    const std::uint8_t pattern[] = { 0x00, 0x55, 0xAA, 0xFF };
    assert(brick::core::storage::write_verify(fs, "/sdcard/verify.bin", pattern, sizeof(pattern)) == Status::ok);

    assert(fs.probe("/sdcard/data.bin") && !fs.probe("/sdcard/missing.bin"));
    file.close();
    fs.unmount();
    assert(!fs.mounted() && !fs.probe("/sdcard/data.bin"));
    std::filesystem::remove_all(root);
}

void test_serial_port()
{
    brick::platform::pc::PcSerialPort port;
    assert(port.begin() == Status::ok);
    const std::uint8_t frame[3] = { 0xA5, 1, 50 };
    port.inject(frame, sizeof(frame));
    std::uint8_t buffer[2];
    assert(port.read(buffer, sizeof(buffer)) == 2 && buffer[0] == 0xA5 && buffer[1] == 1);
    assert(port.read(buffer, sizeof(buffer)) == 1 && buffer[0] == 50);
    assert(port.read(buffer, sizeof(buffer)) == 0);
    assert(port.write(frame, 2) == 2 && port.output().size() == 2);
    brick::platform::pc::PcSerialPort missing("does-not-exist.bin");
    assert(missing.begin() == Status::not_found);
}

void test_file_asset_source()
{
    const char* const path = "pc_asset_bundle.bin";
    {
        std::ofstream bundle(path, std::ios::binary);
        const char    bytes[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        bundle.write(bytes, sizeof(bytes));
    }
    brick::platform::pc::PcFileAssetSource source(path);
    assert(source.begin() == Status::ok && source.size() == 8);
    brick::interfaces::display::AssetDescriptor asset;
    asset.offset        = 4;
    asset.size          = 4;
    std::uint8_t out[4] = {};
    assert(source.read(asset, 1, out, 3) == Status::ok && out[0] == 5 && out[2] == 7);
    assert(source.read(asset, 2, out, 3) == Status::invalid_argument);
    asset.offset = 6;
    assert(source.read(asset, 0, out, 4) == Status::invalid_argument);
    std::filesystem::remove(path);
    brick::platform::pc::PcFileAssetSource missing("does-not-exist.bin");
    assert(missing.begin() == Status::not_found);
}

void test_memory_and_time()
{
    brick::platform::pc::PcMemory memory(128);
    void*                         block = memory.allocate(64, brick::interfaces::memory::MemoryKind::dma, 32);
    assert(block != nullptr && reinterpret_cast<std::uintptr_t>(block) % 32 == 0);
    assert(memory.allocate(100, brick::interfaces::memory::MemoryKind::external, 4) == nullptr);  // over budget
    memory.release(block);
    assert(memory.live_blocks() == 0);

    {
        brick::core::memory::MemoryBlock owned(memory);
        assert(owned.allocate(32, brick::interfaces::memory::MemoryKind::external) == Status::ok && owned.size() == 32);
        // Already holds a block.
        assert(owned.allocate(16, brick::interfaces::memory::MemoryKind::external) == Status::invalid_argument);
        assert(memory.live_blocks() == 1);
    }
    assert(memory.live_blocks() == 0);  // released by the destructor

    brick::platform::pc::PcTime time(true);
    assert(time.millis() == 0);
    time.delay_ms(25);
    assert(time.millis() == 25 && time.micros() == 25000);
}

/// Minimal application used to exercise the simulator loop.
class CountingApplication
{
public:
    explicit CountingApplication(brick::boards::SimulatorBoard& board) : board_(board) {}

    Status initialize() { return board_.begin(); }

    bool update()
    {
        TouchPoint  point;
        std::size_t count = 0;
        if (board_.touch().read(&point, 1, count) == Status::ok && count == 1)
            ++touches;
        // Fill the screen with the number of updates so far.
        const std::uint16_t        value = static_cast<std::uint16_t>(++updates);
        std::vector<std::uint16_t> row(board_.display().size().width, value);
        const PixelBuffer          buffer{ reinterpret_cast<const std::uint8_t*>(row.data()),
                                  static_cast<std::uint32_t>(row.size()), 1, row.size() * 2 };
        for (std::int32_t y = 0; y < board_.display().size().height; ++y)
            assert(board_.display().draw_buffer(DisplayRect{ 0, y, static_cast<std::int32_t>(row.size()), 1 }, buffer)
                   == Status::ok);
        return true;
    }

    int updates = 0;
    int touches = 0;

private:
    brick::boards::SimulatorBoard& board_;
};

void test_headless_simulation()
{
    brick::platform::pc::SimulatorOptions options;
    char                                  program[]  = "app";
    char                                  headless[] = "--headless";
    char                                  frames[]   = "--frames";
    char                                  count[]    = "10";
    char                                  manual[]   = "--manual-time";
    char                                  step[]     = "10";
    char                                  touch[]    = "--touch";
    char                                  where[]    = "5,6@30+20";
    char                                  shot[]     = "--screenshot";
    char                                  file[]     = "pc_test_screenshot.ppm";
    char* argv[] = { program, headless, frames, count, manual, step, touch, where, shot, file };
    assert(brick::platform::pc::SimulatorOptions::parse(10, argv, options));
    assert(options.headless && options.frames == 10 && options.manual_time && options.step_ms == 10);
    assert(options.touches.size() == 1 && options.touches[0].x == 5 && options.touches[0].at_ms == 30);

    brick::boards::SimulatorBoard board(brick::boards::SimulatorBoardConfig::esp12f_st7789(), options);
    CountingApplication           application(board);
    assert(brick::platform::pc::run(application, board.simulator()) == 0);
    assert(application.updates == 10);
    assert(application.touches == 3);  // pressed at 30 ms, moved/held at 40 ms, released at 50 ms
    assert(board.time().millis() == 90);

    std::ifstream screenshot(file, std::ios::binary);
    std::string   header;
    screenshot >> header;
    assert(header == "P6");
    screenshot.close();
    std::remove(file);

    char                                  bad[]      = "--frames";
    char*                                 bad_argv[] = { program, bad };
    brick::platform::pc::SimulatorOptions rejected;
    assert(!brick::platform::pc::SimulatorOptions::parse(2, bad_argv, rejected));
}

}  // namespace

int main()
{
    test_partial_display();
    test_frame_buffer_display();
    test_touch_and_button();
    test_file_system();
    test_serial_port();
    test_file_asset_source();
    test_memory_and_time();
    test_headless_simulation();
    std::puts("brick_pc_tests: OK");
    return 0;
}
