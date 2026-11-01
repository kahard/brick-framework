#include "brick/core/network/HttpProbe.h"

#include <cstdio>
#include <cstring>

namespace brick::core::network
{

namespace
{
    constexpr char        kRequestFormat[] = "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: brick-http-probe\r\n"
                                             "Connection: close\r\n\r\n";
    constexpr char        kStatusPrefix[]  = "HTTP/1.";
    constexpr char        kLineEnd[]       = "\r\n";
    constexpr std::size_t kPrefixLength    = sizeof(kStatusPrefix) - 1;
    /// "HTTP/1.1 204": the code starts after the minor version and a space.
    constexpr std::size_t kCodeOffset = kPrefixLength + 2;
    constexpr std::size_t kCodeDigits = 3;

    bool is_digit(char character)
    {
        return character >= '0' && character <= '9';
    }
}  // namespace

Status HttpProbe::start(const char* host, const char* path, std::uint16_t port, std::uint32_t timeout_ms)
{
    cancel();
    if (host == nullptr || path == nullptr || host[0] == '\0' || path[0] == '\0')
        return Status::invalid_argument;
    const int length = std::snprintf(request_, sizeof(request_), kRequestFormat, path, host);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(request_))
        return Status::invalid_argument;
    request_size_  = static_cast<std::size_t>(length);
    request_sent_  = 0;
    response_size_ = 0;
    response_[0]   = '\0';
    http_status_   = 0;
    port_          = port;
    address_       = {};
    timings_       = {};
    started_ms_    = time_.millis();
    if (const Status status = dns_.resolve(host); !is_ok(status))
    {
        finish_(status);
        return status;
    }
    timer_.start(timeout_ms);
    state_  = HttpProbeState::resolving;
    result_ = Status::busy;
    return Status::ok;
}

void HttpProbe::update()
{
    if (!running())
        return;
    if (timer_.expired())
    {
        finish_(Status::timeout);
        return;
    }
    switch (state_)
    {
        case HttpProbeState::resolving:
            update_resolving_();
            break;
        case HttpProbeState::connecting:
            update_connecting_();
            break;
        case HttpProbeState::sending:
            update_sending_();
            break;
        case HttpProbeState::receiving:
            update_receiving_();
            break;
        case HttpProbeState::idle:
        case HttpProbeState::done:
            break;
    }
}

void HttpProbe::cancel()
{
    if (!running())
        return;
    dns_.cancel();
    socket_.close();
    timer_.stop();
    state_  = HttpProbeState::idle;
    result_ = Status::not_ready;
}

void HttpProbe::update_resolving_()
{
    const Status status = dns_.result(address_);
    if (status == Status::busy)
        return;
    if (!is_ok(status))
    {
        finish_(status);
        return;
    }
    timings_.resolved_ms = elapsed_ms_();
    if (const Status connect_status = socket_.connect({ address_, port_ }); !is_ok(connect_status))
    {
        finish_(connect_status);
        return;
    }
    state_ = HttpProbeState::connecting;
}

void HttpProbe::update_connecting_()
{
    const Status status = socket_.poll_connect();
    if (status == Status::busy)
        return;
    if (!is_ok(status))
    {
        finish_(status);
        return;
    }
    timings_.connected_ms = elapsed_ms_();
    state_                = HttpProbeState::sending;
}

void HttpProbe::update_sending_()
{
    std::size_t  sent   = 0;
    const Status status = socket_.send(reinterpret_cast<const std::uint8_t*>(request_) + request_sent_,
                                       request_size_ - request_sent_, sent);
    if (status == Status::busy)
        return;
    if (!is_ok(status))
    {
        finish_(status);
        return;
    }
    request_sent_ += sent;
    if (request_sent_ == request_size_)
        state_ = HttpProbeState::receiving;
}

void HttpProbe::update_receiving_()
{
    std::size_t  received = 0;
    const Status status   = socket_.receive(reinterpret_cast<std::uint8_t*>(response_) + response_size_,
                                            sizeof(response_) - 1 - response_size_, received);
    if (status == Status::no_data)
        return;
    // not_ready: the server closed the connection; judge what arrived.
    if (!is_ok(status) && status != Status::not_ready)
    {
        finish_(status);
        return;
    }
    response_size_ += received;
    response_[response_size_] = '\0';
    const bool line_complete  = std::strstr(response_, kLineEnd) != nullptr;
    if (line_complete || status == Status::not_ready || response_size_ == sizeof(response_) - 1)
        finish_(parse_status_line_() ? Status::ok : Status::error);
}

void HttpProbe::finish_(Status status)
{
    dns_.cancel();
    socket_.close();
    timer_.stop();
    timings_.total_ms = elapsed_ms_();
    result_           = status;
    state_            = HttpProbeState::done;
}

bool HttpProbe::parse_status_line_()
{
    if (response_size_ < kCodeOffset + kCodeDigits || std::strncmp(response_, kStatusPrefix, kPrefixLength) != 0)
        return false;
    std::uint16_t code = 0;
    for (std::size_t index = kCodeOffset; index < kCodeOffset + kCodeDigits; ++index)
    {
        if (!is_digit(response_[index]))
            return false;
        code = static_cast<std::uint16_t>(code * 10 + (response_[index] - '0'));
    }
    http_status_ = code;
    return true;
}

std::uint32_t HttpProbe::elapsed_ms_() const
{
    return time_.millis() - started_ms_;
}

}  // namespace brick::core::network
