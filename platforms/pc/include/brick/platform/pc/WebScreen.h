#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace brick::platform::pc
{

class PcFrameBufferDisplay;
class PcTouchscreen;
class PcButton;

/// Shows the simulated display in a web browser and takes touch and button
/// input from it: a small HTTP + WebSocket server on its own thread, without
/// external libraries. The page draws the image on a canvas; only the rows
/// that changed are sent. The simulator calls apply_input() before every
/// application update and publish() after it.
class WebScreen final
{
public:
    WebScreen();
    ~WebScreen();
    WebScreen(const WebScreen&)            = delete;
    WebScreen& operator=(const WebScreen&) = delete;

    /// Listens on port (0: any free port) of this computer only, or of every
    /// network interface with lan. False when the port cannot be opened.
    bool start(std::uint16_t port, bool lan, const std::string& title);
    /// The port listened on, after start().
    std::uint16_t port() const;
    /// Browsers currently showing the screen.
    std::size_t viewer_count() const;

    /// Shares the visible image and backlight level when they changed.
    void publish(const PcFrameBufferDisplay& display, std::uint8_t brightness_percent);
    /// Applies the touches and button presses received since the last call.
    void apply_input(PcTouchscreen* touch, PcButton* button);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace brick::platform::pc
