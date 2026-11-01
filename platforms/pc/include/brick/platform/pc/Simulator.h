#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "brick/Status.h"
#include "brick/platform/pc/PcFrameBufferDisplay.h"
#include "brick/platform/pc/PcInput.h"
#include "brick/platform/pc/PcTime.h"
#include "brick/platform/pc/PcWifi.h"
#include "brick/platform/pc/WebScreen.h"

namespace brick::platform::pc
{

/// A touch held at (x, y) from at_ms for duration_ms.
struct ScriptedTouch
{
    std::uint32_t at_ms       = 0;
    std::uint32_t duration_ms = 100;
    std::int16_t  x           = 0;
    std::int16_t  y           = 0;
};

/// A button press from at_ms for duration_ms.
struct ScriptedPress
{
    std::uint32_t at_ms       = 0;
    std::uint32_t duration_ms = 100;
};

struct SimulatorOptions
{
    bool                       headless    = false;
    std::uint32_t              frames      = 0;      ///< stop after this many updates; 0 = run until closed
    bool                       manual_time = false;  ///< deterministic time: each update advances step_ms
    std::uint32_t              step_ms     = 16;
    std::uint32_t              scale       = 0;  ///< window zoom; 0 = choose automatically
    std::string                screenshot;       ///< PPM file written when the simulator stops
    std::string                fs_root = "sdcard";
    std::string                serial_input;  ///< file whose bytes the serial port receives
    std::vector<ScriptedTouch> touches;
    std::vector<ScriptedPress> presses;
    bool                       offline  = false;  ///< no internet: lookups and connections fail
    std::uint16_t              web_port = 0;      ///< serve the screen to a browser on this port; 0 = off
    bool                       web_lan  = false;  ///< web screen on every network interface, not only this PC
    std::vector<PcWifiNetwork> wifi_networks;     ///< what a Wi-Fi scan finds; empty = PcWifi defaults

    /// Parses the common simulator options; returns false (after printing
    /// usage) on --help or an invalid option.
    static bool parse(int argc, char** argv, SimulatorOptions& options);
};

/// Drives a simulated board: shows the display in a window (SDL2, when
/// available) or in a web browser (--web), maps mouse, finger and keyboard to
/// touch and button, replays scripted input and stops after the requested
/// number of frames.
class Simulator final
{
public:
    Simulator(const SimulatorOptions& options, PcFrameBufferDisplay& display, PcTime& time,
              const char* title = "BRICK simulator");
    ~Simulator();
    Simulator(const Simulator&)            = delete;
    Simulator& operator=(const Simulator&) = delete;

    void attach(PcTouchscreen& touch) { touch_ = &touch; }
    void attach(PcButton& button) { button_ = &button; }
    void attach(PcBacklight& backlight) { backlight_ = &backlight; }

    /// Opens the window unless running headless. False if a window was
    /// requested but could not be opened.
    bool begin();
    /// One simulator iteration: input, scripted events, repaint, frame
    /// accounting. Returns false when the run should stop.
    bool step();
    /// Writes the screenshot if requested. Returns false if writing failed.
    bool finish();

    std::uint32_t updates() const { return updates_; }
    bool          has_window() const;

    /// Writes the visible display image as a binary PPM file.
    static bool write_ppm(const PcFrameBufferDisplay& display, const std::string& path);

private:
    struct Window;

    void apply_script();
    bool start_web();
    // Window only: false when the user closed it.
    bool handle_events();
    void repaint();

    SimulatorOptions           options_;
    PcFrameBufferDisplay&      display_;
    PcTime&                    time_;
    std::string                title_;
    PcTouchscreen*             touch_     = nullptr;
    PcButton*                  button_    = nullptr;
    PcBacklight*               backlight_ = nullptr;
    std::unique_ptr<Window>    window_;
    std::unique_ptr<WebScreen> web_;
    std::uint32_t              updates_         = 0;
    bool                       script_touching_ = false;
    bool                       script_pressing_ = false;
};

/// Runs an application with Status initialize() and update() under the
/// simulator. update() may return void or bool (false stops the run).
template <typename Application>
int run(Application& application, Simulator& simulator)
{
    if (!simulator.begin() || !is_ok(application.initialize()))
        return 1;
    while (simulator.step())
    {
        if constexpr (std::is_same_v<decltype(application.update()), bool>)
        {
            if (!application.update())
                break;
        }
        else
        {
            application.update();
        }
    }
    return simulator.finish() ? 0 : 1;
}

}  // namespace brick::platform::pc
