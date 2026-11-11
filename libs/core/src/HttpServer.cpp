#include "brick/core/network/HttpServer.h"

#include <cstring>

namespace brick::core::network
{

namespace
{
    constexpr char kTag[] = "brick_http_server";

    const char* reason(std::uint16_t status)
    {
        switch (status)
        {
            case 200:
                return "OK";
            case 302:
                return "Found";
            case 400:
                return "Bad Request";
            case 404:
                return "Not Found";
            case 405:
                return "Method Not Allowed";
            case 408:
                return "Request Timeout";
            case 413:
                return "Content Too Large";
            case 431:
                return "Request Header Fields Too Large";
            default:
                return "Error";
        }
    }

    char lower(char character)
    {
        return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
    }

    /// true when line starts with name (lower case, ending in ':'), any case.
    bool header_is(const char* line, const char* name)
    {
        for (; *name != '\0'; ++line, ++name)
            if (lower(*line) != *name)
                return false;
        return true;
    }

    const char* skip_spaces(const char* text)
    {
        while (*text == ' ' || *text == '\t')
            ++text;
        return text;
    }

    int hex_digit(char character)
    {
        if (character >= '0' && character <= '9')
            return character - '0';
        if (character >= 'a' && character <= 'f')
            return character - 'a' + 10;
        if (character >= 'A' && character <= 'F')
            return character - 'A' + 10;
        return -1;
    }
}  // namespace

HttpResponse::HttpResponse(char* buffer, std::size_t capacity) : buffer_(buffer), capacity_(capacity)
{
    if (capacity_ > 0)
        buffer_[0] = '\0';
}

HttpResponse& HttpResponse::start(std::uint16_t status, const char* content_type)
{
    started_ = true;
    text("HTTP/1.1 ").number(status).text(" ").text(reason(status));
    text("\r\nContent-Type: ").text(content_type);
    return text("\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
}

HttpResponse& HttpResponse::redirect(const char* location)
{
    started_ = true;
    text("HTTP/1.1 302 Found\r\nLocation: ").text(location);
    return text("\r\nContent-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
}

HttpResponse& HttpResponse::text(const char* text)
{
    append_(text, std::strlen(text));
    return *this;
}

HttpResponse& HttpResponse::escaped(const char* text)
{
    for (; *text != '\0'; ++text)
    {
        switch (*text)
        {
            case '&':
                append_("&amp;", 5);
                break;
            case '<':
                append_("&lt;", 4);
                break;
            case '>':
                append_("&gt;", 4);
                break;
            case '"':
                append_("&quot;", 6);
                break;
            case '\'':
                append_("&#39;", 5);
                break;
            default:
                append_(text, 1);
                break;
        }
    }
    return *this;
}

HttpResponse& HttpResponse::number(long value)
{
    char          digits[24];
    std::size_t   count     = 0;
    const bool    negative  = value < 0;
    unsigned long magnitude = negative ? 0UL - static_cast<unsigned long>(value) : static_cast<unsigned long>(value);
    do
    {
        digits[count++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (negative)
        digits[count++] = '-';
    while (count > 0)
        append_(&digits[--count], 1);
    return *this;
}

void HttpResponse::append_(const char* text, std::size_t length)
{
    if (capacity_ == 0)
    {
        truncated_ = truncated_ || length > 0;
        return;
    }
    const std::size_t room = capacity_ - 1 - size_;
    if (length > room)
    {
        length     = room;
        truncated_ = true;
    }
    std::memcpy(buffer_ + size_, text, length);
    size_ += length;
    buffer_[size_] = '\0';
}

bool http_form_value(const char* form, const char* name, char* value, std::size_t capacity)
{
    if (capacity == 0)
        return false;
    value[0]                 = '\0';
    const std::size_t length = std::strlen(name);
    for (const char* field = form; field != nullptr && *field != '\0';)
    {
        const char* end = std::strchr(field, '&');
        if (std::strncmp(field, name, length) == 0 && field[length] == '=')
        {
            std::size_t size = 0;
            for (const char* text = field + length + 1; text != end && *text != '\0'; ++text)
            {
                char decoded = *text == '+' ? ' ' : *text;
                if (*text == '%' && hex_digit(text[1]) >= 0 && hex_digit(text[2]) >= 0)
                {
                    decoded = static_cast<char>(hex_digit(text[1]) * 16 + hex_digit(text[2]));
                    text += 2;
                }
                if (size + 1 >= capacity)
                {
                    value[0] = '\0';
                    return false;
                }
                value[size++] = decoded;
            }
            value[size] = '\0';
            return true;
        }
        field = end == nullptr ? nullptr : end + 1;
    }
    return false;
}

HttpServer::HttpServer(interfaces::network::ITcpListener& listener, interfaces::time::ITimeProvider& time,
                       interfaces::logging::ILogger& logger, IHttpHandler& handler, char* request_buffer,
                       std::size_t request_capacity, char* response_buffer, std::size_t response_capacity)
    : listener_(listener), logger_(logger), handler_(handler), timer_(time), request_(request_buffer),
      request_capacity_(request_capacity), response_(response_buffer), response_capacity_(response_capacity)
{
}

Status HttpServer::start(std::uint16_t port)
{
    if (request_ == nullptr || request_capacity_ < 64 || response_ == nullptr || response_capacity_ < 128)
        return Status::invalid_argument;
    stop();
    if (const Status status = listener_.listen(port); !is_ok(status))
        return status;
    running_ = true;
    served_  = 0;
    return Status::ok;
}

void HttpServer::stop()
{
    if (client_ != nullptr)
        client_->close();
    client_ = nullptr;
    state_  = State::idle;
    if (running_)
        listener_.close();
    running_ = false;
}

void HttpServer::update()
{
    if (!running_)
        return;
    if (state_ == State::idle)
    {
        client_ = listener_.accept();
        if (client_ == nullptr)
            return;
        state_    = State::reading;
        received_ = 0;
        timer_.start(kRequestTimeoutMs);
    }
    if (state_ == State::reading)
        read_();
    if (state_ == State::sending)
        send_();
}

void HttpServer::read_()
{
    for (;;)
    {
        std::size_t  count  = 0;
        const Status status = client_->receive(reinterpret_cast<std::uint8_t*>(request_ + received_),
                                               request_capacity_ - 1 - received_, count);
        if (is_ok(status) && count > 0)
        {
            received_ += count;
            HttpRequest request;
            if (complete_request_(request))
            {
                HttpResponse response(response_, response_capacity_);
                handler_.handle(request, response);
                if (!response.started())
                    response.start(404, "text/plain").text("Not found\n");
                if (response.truncated())
                    logger_.warning(kTag, "Response to %s cut at %u bytes", request.path,
                                    static_cast<unsigned>(response.size()));
                response_size_ = response.size();
                sent_          = 0;
                state_         = State::sending;
                timer_.start(kRequestTimeoutMs);
                ++served_;
            }
            if (state_ != State::reading || received_ + 1 >= request_capacity_)
                return;
            continue;
        }
        if (status == Status::no_data || (is_ok(status) && count == 0))
        {
            if (timer_.expired())
                answer_error_(408);
            return;
        }
        // Closed or broken before the request was complete.
        finish_();
        return;
    }
}

bool HttpServer::complete_request_(HttpRequest& request)
{
    request_[received_]        = '\0';
    char* const end_of_headers = std::strstr(request_, "\r\n\r\n");
    if (end_of_headers == nullptr)
    {
        if (received_ + 1 >= request_capacity_)
            answer_error_(431);
        return false;
    }
    const std::size_t header_bytes = static_cast<std::size_t>(end_of_headers - request_) + 4;
    std::size_t       body_size    = 0;
    for (const char* line = std::strstr(request_, "\r\n") + 2; line < end_of_headers;
         line             = std::strstr(line, "\r\n") + 2)
        if (header_is(line, "content-length:"))
            for (const char* digit = skip_spaces(line + 15); *digit >= '0' && *digit <= '9'; ++digit)
                body_size = body_size * 10 + static_cast<std::size_t>(*digit - '0');
    if (body_size > request_capacity_ - 1 - header_bytes)
    {
        answer_error_(413);
        return false;
    }
    if (received_ < header_bytes + body_size)
        return false;

    // The whole request is here: cut it into strings in place.
    request_[header_bytes + body_size] = '\0';
    request.body                       = request_ + header_bytes;
    request.body_size                  = body_size;
    for (char* line = std::strstr(request_, "\r\n") + 2; line < end_of_headers;)
    {
        char* const next = std::strstr(line, "\r\n");
        *next            = '\0';
        if (header_is(line, "host:"))
        {
            char* const host = const_cast<char*>(skip_spaces(line + 5));
            if (char* const port = std::strchr(host, ':'); port != nullptr)
                *port = '\0';
            request.host = host;
        }
        line = next + 2;
    }
    char* const line_end = std::strstr(request_, "\r\n");
    *line_end            = '\0';
    char* const target   = std::strchr(request_, ' ');
    if (target == nullptr)
    {
        answer_error_(400);
        return false;
    }
    *target        = '\0';
    request.method = std::strcmp(request_, "GET") == 0  ? HttpMethod::get :
                     std::strcmp(request_, "POST") == 0 ? HttpMethod::post :
                                                          HttpMethod::other;
    if (char* const version = std::strchr(target + 1, ' '); version != nullptr)
        *version = '\0';
    request.path = target + 1;
    if (char* const query = std::strchr(target + 1, '?'); query != nullptr)
    {
        *query        = '\0';
        request.query = query + 1;
    }
    return true;
}

void HttpServer::answer_error_(std::uint16_t status)
{
    HttpResponse response(response_, response_capacity_);
    response.start(status, "text/plain").text(reason(status)).text("\n");
    response_size_ = response.size();
    sent_          = 0;
    state_         = State::sending;
    timer_.start(kRequestTimeoutMs);
    logger_.warning(kTag, "Request answered with %u", static_cast<unsigned>(status));
}

void HttpServer::send_()
{
    while (sent_ < response_size_)
    {
        std::size_t  count = 0;
        const Status status =
            client_->send(reinterpret_cast<const std::uint8_t*>(response_ + sent_), response_size_ - sent_, count);
        if (is_ok(status) && count > 0)
        {
            sent_ += count;
            timer_.start(kRequestTimeoutMs);
            continue;
        }
        if ((status == Status::busy || is_ok(status)) && !timer_.expired())
            return;
        break;
    }
    finish_();
}

void HttpServer::finish_()
{
    client_->close();
    client_ = nullptr;
    state_  = State::idle;
}

}  // namespace brick::core::network
