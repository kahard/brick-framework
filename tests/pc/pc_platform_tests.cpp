#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "brick/boards/pc/SimulatorBoard.h"
#include "brick/core/memory/MemoryBlock.h"
#include "brick/platform/pc/PcFileSystem.h"
#include "brick/platform/pc/PcFrameBufferDisplay.h"
#include "brick/platform/pc/PcInput.h"
#include "brick/platform/pc/PcMemory.h"
#include "brick/platform/pc/PcTime.h"
#include "brick/platform/pc/Simulator.h"

namespace
{

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
    assert(display.begin());
    assert(display.size().width == 4 && display.size().height == 2);
    assert(!display.capabilities().scanout_buffers);
    assert(display.frame_buffer_count() == 0);

    const std::uint16_t pixels[2] = { 0xF800, 0x07E0 };
    const PixelBuffer   buffer{ reinterpret_cast<const std::uint8_t*>(pixels), 2, 1, 4 };
    assert(display.draw_buffer(DisplayRect{ 1, 1, 2, 1 }, buffer));
    assert(display.screen()[4 + 1] == 0xF800 && display.screen()[4 + 2] == 0x07E0);
    assert(display.frame_counter() == 1);
    assert(!display.draw_buffer(DisplayRect{ 3, 1, 2, 1 }, buffer));  // outside the screen
    assert(!display.draw_buffer(DisplayRect{ -1, 0, 2, 1 }, buffer));

    assert(display.set_rotation(Rotation::rotate_90));
    assert(display.size().width == 2 && display.size().height == 4);
}

void test_frame_buffer_display()
{
    PcFrameBufferDisplay display({ 3, 2, 2 });
    assert(display.begin());
    assert(display.capabilities().scanout_buffers && display.wait_for_vsync(10));
    WritablePixelBuffer back{};
    assert(display.frame_buffer_count() == 2 && display.get_frame_buffer(1, back));
    assert(back.width == 3 && back.height == 2 && back.stride_bytes == 6);
    reinterpret_cast<std::uint16_t*>(back.data)[5] = 0x1234;
    assert(display.present_frame_buffer(1));
    assert(display.screen()[5] == 0x1234);
    assert(!display.get_frame_buffer(2, back));
}

void test_touch_and_button()
{
    brick::platform::pc::PcTouchscreen touch;
    TouchPoint                         points[2];
    std::size_t                        count = 9;
    assert(!touch.read(points, 2, count) && count == 0);
    touch.set(true, 10, 20);
    assert(touch.read(points, 2, count) && count == 1 && points[0].state == TouchState::pressed);
    assert(points[0].x == 10 && points[0].y == 20);
    assert(!touch.read(points, 2, count));  // no new sample
    touch.set(true, 11, 21);
    assert(touch.read(points, 2, count) && count == 1 && points[0].state == TouchState::moved);
    touch.set(false, 0, 0);
    assert(touch.read(points, 2, count) && count == 0 && !touch.pressed());

    brick::platform::pc::PcButton button;
    assert(button.begin() && !button.is_pressed());
    button.set_pressed(true);
    assert(button.is_pressed());
}

void test_file_system()
{
    const auto root = std::filesystem::path("pc_fs_test");
    std::filesystem::remove_all(root);
    brick::platform::pc::PcFileSystem fs(root.string());
    assert(!fs.mount());
    std::filesystem::create_directories(root);
    assert(fs.mount());

    {
        auto file = fs.open("/sdcard/data.bin", "w");
        assert(file != nullptr);
        const char text[] = "brick";
        assert(file->write(text, 1, 5) == 5);
    }
    auto file = fs.open("/sdcard/data.bin", "r");
    assert(file != nullptr);
    char read_back[6] = {};
    assert(file->read(read_back, 1, 5) == 5 && std::strcmp(read_back, "brick") == 0);
    assert(file->seek(1, SEEK_SET) && file->read(read_back, 1, 1) == 1 && read_back[0] == 'r');
    const auto names = fs.list_files("/sdcard");
    assert(names.size() == 1 && names[0] == "data.bin");
    assert(fs.open("/sdcard/missing.bin", "r") == nullptr);
    assert(fs.probe("/sdcard/data.bin") && !fs.probe("/sdcard/missing.bin"));
    fs.unmount();
    assert(!fs.mounted() && !fs.probe("/sdcard/data.bin"));
    file.reset();
    std::filesystem::remove_all(root);
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
        assert(owned.allocate(32, brick::interfaces::memory::MemoryKind::external) && owned.size() == 32);
        assert(!owned.allocate(16, brick::interfaces::memory::MemoryKind::external));  // already holds a block
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

    bool initialize() { return board_.begin(); }

    bool update()
    {
        TouchPoint  point;
        std::size_t count = 0;
        if (board_.touch().read(&point, 1, count) && count == 1)
            ++touches;
        // Fill the screen with the number of updates so far.
        const std::uint16_t        value = static_cast<std::uint16_t>(++updates);
        std::vector<std::uint16_t> row(board_.display().size().width, value);
        const PixelBuffer          buffer{ reinterpret_cast<const std::uint8_t*>(row.data()),
                                  static_cast<std::uint32_t>(row.size()), 1, row.size() * 2 };
        for (std::int32_t y = 0; y < board_.display().size().height; ++y)
            board_.display().draw_buffer(DisplayRect{ 0, y, static_cast<std::int32_t>(row.size()), 1 }, buffer);
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
    assert(application.touches == 2);  // pressed at 30 ms, moved/held at 40 ms
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
    test_memory_and_time();
    test_headless_simulation();
    std::puts("brick_pc_tests: OK");
    return 0;
}
