#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/core/time/Timer.h"
#include "brick/interfaces/logging/ILogger.h"
#include "brick/interfaces/network/ITcpListener.h"
#include "brick/interfaces/network/ITcpSocket.h"
#include "brick/interfaces/time/ITimeProvider.h"

namespace brick::core::network
{

enum class HttpMethod : std::uint8_t
{
    get,
    post,
    other,
};

/// A request as HttpServer read it. The strings point into the server's
/// request buffer and are valid only during IHttpHandler::handle().
struct HttpRequest
{
    HttpMethod  method    = HttpMethod::other;
    const char* path      = "";  ///< without the query, e.g. "/connect"
    const char* query     = "";  ///< after '?', still URL-encoded; "" without one
    const char* host      = "";  ///< the Host header without its port; "" without one
    const char* body      = "";  ///< NUL-terminated, e.g. a URL-encoded form
    std::size_t body_size = 0;
};

/// A response written into a fixed buffer: the status line and headers
/// first, then the body. The connection closes after it (no Content-Length
/// needed). Text that does not fit is cut; truncated() tells.
class HttpResponse final
{
public:
    HttpResponse(char* buffer, std::size_t capacity);

    /// Writes the status line and headers; call it once, before the body.
    HttpResponse& start(std::uint16_t status, const char* content_type = "text/html; charset=utf-8");
    /// A complete "302 Found" to location, without a body.
    HttpResponse& redirect(const char* location);
    HttpResponse& text(const char* text);
    /// The text with &, <, >, " and ' as HTML entities, for user data.
    HttpResponse& escaped(const char* text);
    HttpResponse& number(long value);

    const char* data() const { return buffer_; }
    std::size_t size() const { return size_; }
    bool        started() const { return started_; }
    bool        truncated() const { return truncated_; }

private:
    void append_(const char* text, std::size_t length);

    char*       buffer_;
    std::size_t capacity_;
    std::size_t size_      = 0;
    bool        started_   = false;
    bool        truncated_ = false;
};

/// What a server answers; called from HttpServer::update().
class IHttpHandler
{
public:
    virtual ~IHttpHandler()                                                 = default;
    virtual void handle(const HttpRequest& request, HttpResponse& response) = 0;
};

/// Value of field name in a URL-encoded form ("ssid=My+Net&password=a%26b"),
/// decoded into value. false when the field is missing or does not fit
/// (value is then empty).
bool http_form_value(const char* form, const char* name, char* value, std::size_t capacity);

/// Small HTTP/1.1 server for a device's own pages (e.g. Wi-Fi setup): one
/// connection at a time, a request of up to the request buffer (headers and
/// body), a response built whole in the response buffer and sent while
/// update() runs. It never blocks; other connections wait in the listener.
class HttpServer final
{
public:
    /// A client that sends its request slower than this is dropped.
    static constexpr std::uint32_t kRequestTimeoutMs = 5000;

    HttpServer(interfaces::network::ITcpListener& listener, interfaces::time::ITimeProvider& time,
               interfaces::logging::ILogger& logger, IHttpHandler& handler, char* request_buffer,
               std::size_t request_capacity, char* response_buffer, std::size_t response_capacity);

    /// Starts listening on port. Status::invalid_argument without buffers
    /// (the request one needs room for headers, e.g. 1 KB).
    Status start(std::uint16_t port);
    void   stop();
    /// Accepts, reads, answers and closes; call it from the loop.
    void update();
    bool running() const { return running_; }
    /// A client is being read or answered.
    bool busy() const { return state_ != State::idle; }
    /// Requests answered since start().
    std::uint32_t served() const { return served_; }

private:
    enum class State : std::uint8_t
    {
        idle,
        reading,
        sending,
    };

    void read_();
    void send_();
    /// true once the whole request is in the buffer; answers errors itself.
    bool complete_request_(HttpRequest& request);
    void answer_error_(std::uint16_t status);
    void finish_();

    interfaces::network::ITcpListener& listener_;
    interfaces::logging::ILogger&      logger_;
    IHttpHandler&                      handler_;
    core::time::Timer                  timer_;
    char*                              request_;
    std::size_t                        request_capacity_;
    char*                              response_;
    std::size_t                        response_capacity_;
    interfaces::network::ITcpSocket*   client_        = nullptr;
    State                              state_         = State::idle;
    std::size_t                        received_      = 0;
    std::size_t                        response_size_ = 0;
    std::size_t                        sent_          = 0;
    std::uint32_t                      served_        = 0;
    bool                               running_       = false;
};

}  // namespace brick::core::network
