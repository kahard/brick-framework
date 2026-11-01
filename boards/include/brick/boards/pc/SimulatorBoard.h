#pragma once

#include <cstdint>

#include "brick/interfaces/board/IBoard.h"
#include "brick/platform/pc/PcFileSystem.h"
#include "brick/platform/pc/PcFrameBufferDisplay.h"
#include "brick/platform/pc/PcInput.h"
#include "brick/platform/pc/PcLogger.h"
#include "brick/platform/pc/PcMemory.h"
#include "brick/platform/pc/PcSerialPort.h"
#include "brick/platform/pc/PcTime.h"
#include "brick/platform/pc/Simulator.h"

namespace brick::boards
{

/// What the simulated board looks like. Use the profile of the real board an
/// application targets, e.g. SimulatorBoardConfig::cyd().
struct SimulatorBoardConfig
{
    const char*   name          = "PC simulator";
    std::uint16_t width         = 320;
    std::uint16_t height        = 240;
    std::uint8_t  frame_buffers = 0;  ///< 0 = partial-update panel, N = scan-out frame buffers
    bool          debug_log     = false;

    static constexpr SimulatorBoardConfig cyd() { return { "CYD 2.8-inch (simulated)", 320, 240, 0, false }; }
    static constexpr SimulatorBoardConfig panel480() { return { "ESP32-S3 480x480 (simulated)", 480, 480, 2, false }; }
    static constexpr SimulatorBoardConfig jc1060() { return { "JC1060 7-inch (simulated)", 1024, 600, 2, false }; }
    static constexpr SimulatorBoardConfig jc8012() { return { "JC8012 10-inch (simulated)", 800, 1280, 2, false }; }
    static constexpr SimulatorBoardConfig esp12f_st7789()
    {
        return { "ESP-12F ST7789 (simulated)", 240, 240, 0, false };
    }
};

/// Board for the PC simulator. It offers the same accessors as the hardware
/// boards (display(), touch(), backlight(), button(), sd(), time(), logger())
/// so a target entry point for the PC looks like the embedded one.
class SimulatorBoard final : public brick::interfaces::board::IBoard
{
public:
    SimulatorBoard(const SimulatorBoardConfig& config, const brick::platform::pc::SimulatorOptions& options)
        : config_(config), time_(options.manual_time), logger_(config.debug_log),
          display_({ config.width, config.height, config.frame_buffers }), sd_(options.fs_root),
          serial_(options.serial_input), simulator_(options, display_, time_, config.name)
    {
        simulator_.attach(touch_);
        simulator_.attach(button_);
        simulator_.attach(backlight_);
    }

    brick::interfaces::board::BoardDescriptor descriptor() const override
    {
        using brick::interfaces::board::Capability;
        return { config_.name, "PC",
                 static_cast<std::uint32_t>(Capability::display) | static_cast<std::uint32_t>(Capability::touchscreen)
                     | static_cast<std::uint32_t>(Capability::backlight)
                     | static_cast<std::uint32_t>(Capability::sd_card) };
    }

    /// Starts every peripheral, even after a failure, and returns the first
    /// failure (Status::ok when all started).
    Status begin() override
    {
        const Status results[] = { display_.begin(), backlight_.begin(), touch_.begin(), button_.begin(),
                                   serial_.begin() };
        for (const Status status : results)
            if (!is_ok(status))
                return status;
        return Status::ok;
    }

    brick::interfaces::display::IDisplayDevice* display_device() override { return &display_; }
    brick::interfaces::display::ITouchscreen*   touchscreen() override { return &touch_; }

    brick::platform::pc::PcFrameBufferDisplay& display() { return display_; }
    brick::platform::pc::PcTouchscreen&        touch() { return touch_; }
    brick::platform::pc::PcBacklight&          backlight() { return backlight_; }
    brick::platform::pc::PcButton&             button() { return button_; }
    brick::platform::pc::PcFileSystem&         sd() { return sd_; }
    brick::platform::pc::PcMemory&             memory() { return memory_; }
    brick::platform::pc::PcSerialPort&         serial() { return serial_; }
    brick::platform::pc::PcTime&               time() { return time_; }
    brick::platform::pc::PcLogger&             logger() { return logger_; }
    brick::platform::pc::Simulator&            simulator() { return simulator_; }

private:
    SimulatorBoardConfig                      config_;
    brick::platform::pc::PcTime               time_;
    brick::platform::pc::PcLogger             logger_;
    brick::platform::pc::PcFrameBufferDisplay display_;
    brick::platform::pc::PcTouchscreen        touch_;
    brick::platform::pc::PcBacklight          backlight_;
    brick::platform::pc::PcButton             button_;
    brick::platform::pc::PcFileSystem         sd_;
    brick::platform::pc::PcSerialPort         serial_;
    brick::platform::pc::PcMemory             memory_;
    brick::platform::pc::Simulator            simulator_;
};

}  // namespace brick::boards
