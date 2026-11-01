#include "brick/platform/pc/WebScreen.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "SocketSupport.h"
#include "WebScreenPage.h"
#include "WebSocketFrames.h"
#include "brick/platform/pc/PcFrameBufferDisplay.h"
#include "brick/platform/pc/PcInput.h"

namespace brick::platform::pc
{

namespace
{
    using sockets::Handle;
    using sockets::kInvalidHandle;

    constexpr long          kPollUs        = 10000;    ///< longest wait of the server loop
    constexpr std::size_t   kMaxRequest    = 16384;    ///< an HTTP request larger than this is refused
    constexpr std::size_t   kMaxIncoming   = 1 << 20;  ///< unread WebSocket data from one browser
    constexpr std::size_t   kReceiveChunk  = 4096;
    constexpr int           kListenBacklog = 8;
    constexpr std::uint8_t  kSizeMessage   = 1;
    constexpr std::uint8_t  kRectMessage   = 2;
    constexpr std::uint8_t  kBacklight     = 3;
    constexpr std::uint8_t  kUnknownLevel  = 255;
    constexpr std::uint32_t kNotPublished  = 0xFFFFFFFFU;

    /// The image the browsers should show, written by the simulator thread.
    struct Image
    {
        std::uint32_t              version    = 0;
        std::uint16_t              width      = 0;
        std::uint16_t              height     = 0;
        std::uint8_t               brightness = 100;
        std::vector<std::uint16_t> pixels;
    };

    struct Input
    {
        char kind = 0;  ///< 'd', 'm', 'u' (touch) or 'b' (button)
        int  x    = 0;  ///< x, or 1 / 0 for the button
        int  y    = 0;
    };

    struct Client
    {
        Handle                     socket = kInvalidHandle;
        std::string                in;
        std::string                out;
        std::size_t                sent      = 0;  ///< bytes of out already sent
        bool                       websocket = false;
        bool                       closing   = false;  ///< close once out is sent
        std::uint32_t              version   = 0;      ///< image version the browser shows
        std::uint16_t              width     = 0;
        std::uint16_t              height    = 0;
        std::uint8_t               level     = kUnknownLevel;
        std::vector<std::uint16_t> shown;  ///< what the browser shows
    };

    void append_u16(std::string& out, std::uint32_t value)
    {
        out += static_cast<char>(value & 0xFFU);
        out += static_cast<char>((value >> 8) & 0xFFU);
    }

    /// Value of an HTTP header (name in lower case), empty when missing.
    std::string header_value(const std::string& request, const char* name)
    {
        std::string lower = request;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        const std::string key   = std::string("\r\n") + name + ":";
        const std::size_t start = lower.find(key);
        if (start == std::string::npos)
            return {};
        std::size_t       begin = start + key.size();
        const std::size_t end   = request.find("\r\n", begin);
        while (begin < end && request[begin] == ' ')
            ++begin;
        return request.substr(begin, end - begin);
    }

    std::string escape_html(const std::string& text)
    {
        std::string out;
        for (const char character : text)
        {
            switch (character)
            {
                case '<':
                    out += "&lt;";
                    break;
                case '>':
                    out += "&gt;";
                    break;
                case '&':
                    out += "&amp;";
                    break;
                case '"':
                    out += "&quot;";
                    break;
                default:
                    out += character;
            }
        }
        return out;
    }

    std::string http_response(const char* status, const char* type, const std::string& body)
    {
        return std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type + "\r\nContent-Length: "
               + std::to_string(body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
    }

    /// First and one-past-last column where row y of image and shown differ.
    bool changed_span(const Image& image, const Client& client, std::size_t y, std::size_t& left, std::size_t& right)
    {
        const std::uint16_t* now    = image.pixels.data() + y * image.width;
        const std::uint16_t* before = client.shown.data() + y * image.width;
        std::size_t          first  = 0;
        while (first < image.width && now[first] == before[first])
            ++first;
        if (first == image.width)
            return false;
        std::size_t last = image.width;
        while (last > first && now[last - 1] == before[last - 1])
            --last;
        left  = first;
        right = last;
        return true;
    }

    /// One rectangle message for rows [top, bottom), columns [left, right); the
    /// browser's copy is updated to match.
    void append_rect(std::string& out, const Image& image, Client& client, std::size_t left, std::size_t right,
                     std::size_t top, std::size_t bottom)
    {
        out += static_cast<char>(kRectMessage);
        append_u16(out, static_cast<std::uint32_t>(left));
        append_u16(out, static_cast<std::uint32_t>(top));
        append_u16(out, static_cast<std::uint32_t>(right - left));
        append_u16(out, static_cast<std::uint32_t>(bottom - top));
        for (std::size_t y = top; y < bottom; ++y)
        {
            const std::size_t row = y * image.width;
            for (std::size_t x = left; x < right; ++x)
                append_u16(out, image.pixels[row + x]);
            std::copy(image.pixels.begin() + static_cast<std::ptrdiff_t>(row + left),
                      image.pixels.begin() + static_cast<std::ptrdiff_t>(row + right),
                      client.shown.begin() + static_cast<std::ptrdiff_t>(row + left));
        }
    }

    /// Bands of consecutive changed rows, each sent as one rectangle.
    void append_changes(std::string& out, const Image& image, Client& client)
    {
        std::size_t y = 0;
        while (y < image.height)
        {
            std::size_t left  = 0;
            std::size_t right = 0;
            if (!changed_span(image, client, y, left, right))
            {
                ++y;
                continue;
            }
            std::size_t bottom     = y + 1;
            std::size_t next_left  = 0;
            std::size_t next_right = 0;
            while (bottom < image.height && changed_span(image, client, bottom, next_left, next_right))
            {
                left  = std::min(left, next_left);
                right = std::max(right, next_right);
                ++bottom;
            }
            append_rect(out, image, client, left, right, y, bottom);
            y = bottom;
        }
    }
}  // namespace

struct WebScreen::State
{
    std::mutex               mutex;
    Image                    image;   ///< guarded by mutex
    std::vector<Input>       inputs;  ///< guarded by mutex
    std::atomic<bool>        stop{ false };
    std::atomic<std::size_t> viewers{ 0 };
    Handle                   listener = kInvalidHandle;
    std::uint16_t            port     = 0;
    std::string              page;
    std::thread              thread;
    // Simulator thread only.
    std::uint32_t published       = kNotPublished;
    std::uint8_t  published_level = kUnknownLevel;
    // Server thread only.
    std::vector<Client> clients;

    void run();
    void accept_client();
    bool read_client(Client& client);
    void handle_request(Client& client, std::size_t length);
    void handle_frames(Client& client);
    void queue_input(const std::string& text);
    void prepare_update(Client& client);
    bool write_client(Client& client);
};

WebScreen::WebScreen() : state_(std::make_unique<State>())
{
}

WebScreen::~WebScreen()
{
    state_->stop.store(true);
    if (state_->thread.joinable())
        state_->thread.join();
    for (Client& client : state_->clients)
        sockets::close_handle(client.socket);
    if (state_->listener != kInvalidHandle)
        sockets::close_handle(state_->listener);
}

bool WebScreen::start(std::uint16_t port, bool lan, const std::string& title)
{
    sockets::start();
    const Handle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidHandle)
        return false;
#ifndef _WIN32
    const int reuse = 1;  // restart on the same port at once (Windows would allow stealing it)
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(lan ? INADDR_ANY : INADDR_LOOPBACK);
    socklen_t length        = sizeof(address);
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
        || ::listen(listener, kListenBacklog) != 0 || !sockets::make_non_blocking(listener)
        || ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0)
    {
        sockets::close_handle(listener);
        return false;
    }
    state_->listener              = listener;
    state_->port                  = ntohs(address.sin_port);
    state_->page                  = kWebScreenPage;
    const std::string placeholder = "{{TITLE}}";
    for (std::size_t at = state_->page.find(placeholder); at != std::string::npos;
         at             = state_->page.find(placeholder, at))
        state_->page.replace(at, placeholder.size(), escape_html(title));
    state_->thread = std::thread(&State::run, state_.get());
    return true;
}

std::uint16_t WebScreen::port() const
{
    return state_->port;
}

std::size_t WebScreen::viewer_count() const
{
    return state_->viewers.load();
}

void WebScreen::publish(const PcFrameBufferDisplay& display, std::uint8_t brightness_percent)
{
    State& state = *state_;
    if (state.viewers.load() == 0)
    {
        state.published = kNotPublished;  // the next viewer gets a fresh copy
        return;
    }
    if (display.frame_counter() == state.published && brightness_percent == state.published_level)
        return;
    const auto                        size = display.size();
    const std::lock_guard<std::mutex> lock(state.mutex);
    state.image.width  = static_cast<std::uint16_t>(size.width);
    state.image.height = static_cast<std::uint16_t>(size.height);
    state.image.pixels.assign(display.screen(), display.screen() + static_cast<std::size_t>(size.width) * size.height);
    state.image.brightness = brightness_percent;
    ++state.image.version;
    state.published       = display.frame_counter();
    state.published_level = brightness_percent;
}

void WebScreen::apply_input(PcTouchscreen* touch, PcButton* button)
{
    std::vector<Input> inputs;
    {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        inputs.swap(state_->inputs);
    }
    for (const Input& input : inputs)
    {
        const auto x = static_cast<std::int16_t>(input.x);
        const auto y = static_cast<std::int16_t>(input.y);
        if (input.kind == 'b' && button != nullptr)
            button->set_pressed(input.x != 0);
        else if (input.kind == 'd' && touch != nullptr)
            touch->set(true, x, y);
        else if (input.kind == 'm' && touch != nullptr && touch->pressed())
            touch->set(true, x, y);
        else if (input.kind == 'u' && touch != nullptr)
            touch->set(false, x, y);
    }
}

void WebScreen::State::run()
{
    while (!stop.load())
    {
        fd_set readable;
        fd_set writable;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_SET(listener, &readable);
        Handle highest = listener;
        for (const Client& client : clients)
        {
            FD_SET(client.socket, &readable);
            if (client.sent < client.out.size())
                FD_SET(client.socket, &writable);
            highest = std::max(highest, client.socket);
        }
        timeval wait{ 0, kPollUs };
        if (::select(static_cast<int>(highest) + 1, &readable, &writable, nullptr, &wait) < 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(kPollUs));
            continue;
        }
        if (FD_ISSET(listener, &readable))
            accept_client();
        for (Client& client : clients)
        {
            bool keep = !FD_ISSET(client.socket, &readable) || read_client(client);
            if (keep && client.websocket && client.sent == client.out.size())
                prepare_update(client);
            if (keep && client.sent < client.out.size())
                keep = write_client(client);
            if (!keep || (client.closing && client.sent == client.out.size()))
            {
                sockets::close_handle(client.socket);
                client.socket = kInvalidHandle;
            }
        }
        clients.erase(std::remove_if(clients.begin(), clients.end(),
                                     [](const Client& client) { return client.socket == kInvalidHandle; }),
                      clients.end());
        viewers.store(static_cast<std::size_t>(
            std::count_if(clients.begin(), clients.end(), [](const Client& client) { return client.websocket; })));
    }
}

void WebScreen::State::accept_client()
{
    const Handle socket = ::accept(listener, nullptr, nullptr);
    if (socket == kInvalidHandle)
        return;
    if (!sockets::make_non_blocking(socket))
    {
        sockets::close_handle(socket);
        return;
    }
    Client client;
    client.socket = socket;
    clients.push_back(std::move(client));
}

bool WebScreen::State::read_client(Client& client)
{
    char       buffer[kReceiveChunk];
    const auto received = ::recv(client.socket, buffer, static_cast<int>(sizeof(buffer)), 0);
    if (received == 0)
        return false;
    if (received < 0)
        return sockets::would_block(sockets::last_error());
    client.in.append(buffer, static_cast<std::size_t>(received));
    if (client.websocket)
    {
        handle_frames(client);
        return client.in.size() <= kMaxIncoming;
    }
    const std::size_t end = client.in.find("\r\n\r\n");
    if (end != std::string::npos)
        handle_request(client, end + 4);
    return client.in.size() <= kMaxRequest;
}

void WebScreen::State::handle_request(Client& client, std::size_t length)
{
    const std::string request = client.in.substr(0, length);
    client.in.erase(0, length);
    const std::size_t path_start = request.find(' ') + 1;
    const std::string path       = request.substr(path_start, request.find(' ', path_start) - path_start);
    const std::string key        = header_value(request, "sec-websocket-key");
    if (path == "/ws" && !key.empty())
    {
        client.out = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: "
                     + websocket::accept_key(key) + "\r\n\r\n";
        client.websocket = true;
        handle_frames(client);  // frames sent right behind the request
        return;
    }
    client.out     = path == "/" || path == "/index.html" ? http_response("200 OK", "text/html; charset=utf-8", page) :
                                                            http_response("404 Not Found", "text/plain", "not found\n");
    client.closing = true;
}

void WebScreen::State::handle_frames(Client& client)
{
    websocket::Frame frame;
    while (!client.closing && websocket::take_frame(client.in, frame))
    {
        switch (frame.opcode)
        {
            case websocket::Opcode::text:
                queue_input(frame.payload);
                break;
            case websocket::Opcode::ping:
                websocket::append_frame(client.out, websocket::Opcode::pong, frame.payload);
                break;
            case websocket::Opcode::close:
                websocket::append_frame(client.out, websocket::Opcode::close, {});
                client.closing = true;
                break;
            default:
                break;
        }
    }
}

void WebScreen::State::queue_input(const std::string& text)
{
    Input      input;
    const int  fields = std::sscanf(text.c_str(), "%c %d %d", &input.kind, &input.x, &input.y);
    const bool touch  = (input.kind == 'd' || input.kind == 'm' || input.kind == 'u') && fields == 3;
    if (!touch && !(input.kind == 'b' && fields >= 2))
        return;
    const std::lock_guard<std::mutex> lock(mutex);
    if (touch)
    {
        // A pointer dragged off the canvas reports coordinates outside the screen.
        input.x = std::clamp(input.x, 0, std::max(0, static_cast<int>(image.width) - 1));
        input.y = std::clamp(input.y, 0, std::max(0, static_cast<int>(image.height) - 1));
    }
    inputs.push_back(input);
}

void WebScreen::State::prepare_update(Client& client)
{
    std::string message;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (image.version == client.version || image.pixels.empty())
            return;
        if (image.width != client.width || image.height != client.height)
        {
            message += static_cast<char>(kSizeMessage);
            append_u16(message, image.width);
            append_u16(message, image.height);
            client.width  = image.width;
            client.height = image.height;
            client.shown.assign(image.pixels.size(), 0);  // the page clears to black
        }
        if (image.brightness != client.level)
        {
            message += static_cast<char>(kBacklight);
            message += static_cast<char>(image.brightness);
            client.level = image.brightness;
        }
        append_changes(message, image, client);
        client.version = image.version;
    }
    client.out.clear();
    client.sent = 0;
    if (!message.empty())
        websocket::append_frame(client.out, websocket::Opcode::binary, message);
}

bool WebScreen::State::write_client(Client& client)
{
    const std::size_t pending = client.out.size() - client.sent;
    const auto        written = ::send(client.socket, client.out.data() + client.sent,
                                       static_cast<int>(std::min<std::size_t>(pending, 1 << 20)), sockets::kSendFlags);
    if (written > 0)
    {
        client.sent += static_cast<std::size_t>(written);
        if (client.sent == client.out.size())
        {
            client.out.clear();
            client.sent = 0;
        }
        return true;
    }
    return written < 0 && sockets::would_block(sockets::last_error());
}

}  // namespace brick::platform::pc
