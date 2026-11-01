// Tests of the web screen of the PC simulator: the WebSocket helpers against
// the RFC test vectors, then a browser played by a socket on the loopback
// interface: the page, the handshake, the image and touch input.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "SocketSupport.h"
#include "WebSocketFrames.h"
#include "brick/platform/pc/PcFrameBufferDisplay.h"
#include "brick/platform/pc/PcInput.h"
#include "brick/platform/pc/WebScreen.h"

namespace
{

using brick::platform::pc::PcFrameBufferDisplay;
using brick::platform::pc::WebScreen;
namespace sockets   = brick::platform::pc::sockets;
namespace websocket = brick::platform::pc::websocket;

std::string hex(const std::string& bytes)
{
    std::string out;
    char        digits[3];
    for (const char byte : bytes)
    {
        std::snprintf(digits, sizeof(digits), "%02x", static_cast<unsigned char>(byte));
        out += digits;
    }
    return out;
}

void test_websocket_helpers()
{
    assert(hex(websocket::sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    assert(hex(websocket::sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    assert(websocket::base64("foobar") == "Zm9vYmFy" && websocket::base64("fooba") == "Zm9vYmE=");
    assert(websocket::base64("f") == "Zg==");
    // The example of RFC 6455, section 1.3.
    assert(websocket::accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    std::string out;
    websocket::append_frame(out, websocket::Opcode::binary, std::string(125, 'a'));
    assert(out.size() == 127 && static_cast<unsigned char>(out[0]) == 0x82 && out[1] == 125);
    out.clear();
    websocket::append_frame(out, websocket::Opcode::binary, std::string(300, 'a'));
    assert(out.size() == 304 && out[1] == 126 && out[2] == 1 && out[3] == 44);
    out.clear();
    websocket::append_frame(out, websocket::Opcode::binary, std::string(70000, 'a'));
    assert(out.size() == 70010 && out[1] == 127);

    // A masked client frame, as browsers send them, arriving in two parts.
    const std::string payload = "d 3 1";
    const char        mask[]  = { 0x11, 0x22, 0x33, 0x44 };
    std::string       frame   = { static_cast<char>(0x81), static_cast<char>(0x80 | payload.size()) };
    frame.append(mask, 4);
    for (std::size_t index = 0; index < payload.size(); ++index)
        frame += static_cast<char>(payload[index] ^ mask[index % 4]);
    std::string      in = frame.substr(0, 4);
    websocket::Frame received;
    assert(!websocket::take_frame(in, received));
    in += frame.substr(4);
    assert(websocket::take_frame(in, received) && in.empty());
    assert(received.opcode == websocket::Opcode::text && received.payload == payload);
}

/// The browser: a blocking socket with a time limit on every read.
class Browser final
{
public:
    explicit Browser(std::uint16_t port)
    {
        handle_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(::connect(handle_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    }
    ~Browser() { sockets::close_handle(handle_); }

    void send_text(const std::string& text) { ::send(handle_, text.data(), static_cast<int>(text.size()), 0); }

    /// One wait of up to 10 ms for data; calls tick() (the simulator loop)
    /// first. False when the server closed the connection.
    template <typename Tick>
    bool receive_some(Tick tick)
    {
        tick();
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(handle_, &readable);
        timeval wait{ 0, 10000 };
        if (::select(static_cast<int>(handle_) + 1, &readable, nullptr, nullptr, &wait) <= 0)
            return true;
        char       buffer[65536];
        const auto count = ::recv(handle_, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (count <= 0)
            return false;
        received.append(buffer, static_cast<std::size_t>(count));
        return true;
    }

    /// Reads until received contains marker or, with an empty marker, until
    /// the server closes the connection.
    template <typename Tick>
    bool read_until(const std::string& marker, Tick tick)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!marker.empty() && received.find(marker) != std::string::npos)
                return true;
            if (!receive_some(tick))
                return marker.empty();
        }
        return false;
    }

    /// Reads the payload of the next complete WebSocket frame.
    template <typename Tick>
    bool read_message(std::string& payload, Tick tick)
    {
        const auto       deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        websocket::Frame frame;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (websocket::take_frame(received, frame))
            {
                payload = frame.payload;
                return true;
            }
            if (!receive_some(tick))
                return false;
        }
        return false;
    }

    std::string received;

private:
    sockets::Handle handle_ = sockets::kInvalidHandle;
};

std::uint16_t read_u16(const std::string& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset])
                                      | static_cast<unsigned char>(bytes[offset + 1]) << 8);
}

void test_page_and_screen()
{
    PcFrameBufferDisplay display({ 4, 2, 0 });
    assert(display.begin() == brick::Status::ok);
    const std::uint16_t first[8] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0, 0, 0, 0x1234 };
    assert(display.draw_buffer({ 0, 0, 4, 2 }, { reinterpret_cast<const std::uint8_t*>(first), 4, 2, 8 })
           == brick::Status::ok);

    WebScreen screen;
    assert(screen.start(0, false, "Test <board>"));
    assert(screen.port() != 0);
    const auto idle = [] {};

    Browser page(screen.port());
    page.send_text("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert(page.read_until("", idle));
    assert(page.received.rfind("HTTP/1.1 200 OK", 0) == 0);
    assert(page.received.find("<canvas") != std::string::npos);
    assert(page.received.find("Test &lt;board&gt;") != std::string::npos);

    Browser viewer(screen.port());
    viewer.send_text("GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
    assert(viewer.read_until("\r\n\r\n", idle));
    assert(viewer.received.rfind("HTTP/1.1 101", 0) == 0);
    assert(viewer.received.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    viewer.received.erase(0, viewer.received.find("\r\n\r\n") + 4);

    // The first image: size, backlight, then everything that is not black.
    const auto  publish = [&] { screen.publish(display, 80); };
    std::string message;
    assert(viewer.read_message(message, publish));
    assert(message[0] == 1 && read_u16(message, 1) == 4 && read_u16(message, 3) == 2);
    assert(message[5] == 3 && message[6] == 80);
    assert(message[7] == 2 && read_u16(message, 8) == 0 && read_u16(message, 10) == 0);
    assert(read_u16(message, 12) == 4 && read_u16(message, 14) == 2);
    assert(read_u16(message, 16) == 0xF800 && read_u16(message, 30) == 0x1234);
    assert(screen.viewer_count() == 1);

    // One changed pixel: one 1x1 rectangle.
    const std::uint16_t dot = 0xABCD;
    assert(display.draw_buffer({ 1, 1, 1, 1 }, { reinterpret_cast<const std::uint8_t*>(&dot), 1, 1, 2 })
           == brick::Status::ok);
    assert(viewer.read_message(message, publish));
    assert(message.size() == 11 && message[0] == 2);
    assert(read_u16(message, 1) == 1 && read_u16(message, 3) == 1 && read_u16(message, 5) == 1);
    assert(read_u16(message, 9) == 0xABCD);

    // A touch from the browser reaches the touch screen, clamped to the display.
    brick::platform::pc::PcTouchscreen touch;
    brick::platform::pc::PcButton      button;
    const char                         mask[] = { 1, 2, 3, 4 };
    for (const std::string text : { "d 3 1", "m 9 9", "b 1" })
    {
        std::string frame = { static_cast<char>(0x81), static_cast<char>(0x80 | text.size()) };
        frame.append(mask, 4);
        for (std::size_t index = 0; index < text.size(); ++index)
            frame += static_cast<char>(text[index] ^ mask[index % 4]);
        viewer.send_text(frame);
    }
    brick::interfaces::display::TouchPoint point{};
    std::size_t                            count    = 0;
    const auto                             deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!button.is_pressed() && std::chrono::steady_clock::now() < deadline)
    {
        screen.apply_input(&touch, &button);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(button.is_pressed() && touch.pressed());
    assert(touch.read(&point, 1, count) == brick::Status::ok && count == 1);
    assert(point.x == 3 && point.y == 1);  // the press is reported before the move
}

}  // namespace

int main()
{
    sockets::start();
    test_websocket_helpers();
    test_page_and_screen();
    std::puts("brick_pc_web_screen_tests: all passed");
    return 0;
}
