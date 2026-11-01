#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/core/time/Timer.h"
#include "brick/interfaces/network/IDnsResolver.h"
#include "brick/interfaces/network/ITcpSocket.h"
#include "brick/interfaces/time/ITimeProvider.h"

namespace brick::core::network
{

enum class HttpProbeState : std::uint8_t
{
    idle,
    resolving,
    connecting,
    sending,
    receiving,
    done,
};

/// Milliseconds from start() to the end of each step of the last probe.
struct HttpProbeTimings
{
    std::uint32_t resolved_ms  = 0;
    std::uint32_t connected_ms = 0;
    std::uint32_t total_ms     = 0;
};

/// Checks that a web server can be reached, to tell "joined a network" from
/// "the internet works": looks the host up, connects, sends GET over
/// HTTP/1.1 and reads the status code (e.g. connectivitycheck.gstatic.com
/// /generate_204 answers 204). Plain HTTP only. Call update() from the
/// loop; it never blocks.
class HttpProbe final
{
public:
    static constexpr std::size_t kRequestCapacity  = 256;
    static constexpr std::size_t kResponseCapacity = 64;  ///< enough for the status line

    HttpProbe(interfaces::network::IDnsResolver& dns, interfaces::network::ITcpSocket& socket,
              interfaces::time::ITimeProvider& time)
        : dns_(dns), socket_(socket), time_(time), timer_(time)
    {
    }

    /// Starts a probe, cancelling one still running; host and path are
    /// copied. Status::invalid_argument when one is empty or the request does
    /// not fit kRequestCapacity; otherwise what IDnsResolver::resolve() says.
    Status start(const char* host, const char* path, std::uint16_t port = 80, std::uint32_t timeout_ms = 10000);
    void   update();
    void   cancel();

    HttpProbeState state() const { return state_; }
    bool           running() const { return state_ != HttpProbeState::idle && state_ != HttpProbeState::done; }
    /// Status::busy while running; Status::ok when the server answered with
    /// a status line (whatever its code); otherwise why the probe failed
    /// (Status::not_ready: never started).
    Status result() const { return result_; }
    /// Status code of the answer, e.g. 200 or 204; 0 without one.
    std::uint16_t                           http_status() const { return http_status_; }
    const interfaces::network::Ipv4Address& address() const { return address_; }
    const HttpProbeTimings&                 timings() const { return timings_; }

private:
    void          update_resolving_();
    void          update_connecting_();
    void          update_sending_();
    void          update_receiving_();
    void          finish_(Status status);
    bool          parse_status_line_();
    std::uint32_t elapsed_ms_() const;

    interfaces::network::IDnsResolver& dns_;
    interfaces::network::ITcpSocket&   socket_;
    interfaces::time::ITimeProvider&   time_;
    core::time::Timer                  timer_;
    HttpProbeState                     state_       = HttpProbeState::idle;
    Status                             result_      = Status::not_ready;
    std::uint16_t                      port_        = 0;
    std::uint16_t                      http_status_ = 0;
    std::uint32_t                      started_ms_  = 0;
    interfaces::network::Ipv4Address   address_;
    HttpProbeTimings                   timings_;
    char                               request_[kRequestCapacity]   = {};
    std::size_t                        request_size_                = 0;
    std::size_t                        request_sent_                = 0;
    char                               response_[kResponseCapacity] = {};
    std::size_t                        response_size_               = 0;
};

}  // namespace brick::core::network
