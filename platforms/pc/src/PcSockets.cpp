#include "brick/platform/pc/PcSockets.h"

#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <cerrno>
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

#include "brick/core/network/Ipv4.h"

namespace brick::platform::pc
{

using brick::interfaces::network::Ipv4Address;
using brick::interfaces::network::LinkState;

namespace
{
#ifdef _WIN32
    using Handle                    = SOCKET;
    constexpr Handle kInvalidHandle = INVALID_SOCKET;

    void start_sockets()
    {
        static std::once_flag started;
        std::call_once(started,
                       []
                       {
                           WSADATA data;
                           WSAStartup(MAKEWORD(2, 2), &data);
                       });
    }
    int last_error()
    {
        return WSAGetLastError();
    }
    bool would_block(int error)
    {
        return error == WSAEWOULDBLOCK;
    }
    bool connect_pending(int error)
    {
        return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
    }
    bool timed_out(int error)
    {
        return error == WSAETIMEDOUT;
    }
    void close_handle(Handle handle)
    {
        closesocket(handle);
    }
    bool make_non_blocking(Handle handle)
    {
        u_long enabled = 1;
        return ioctlsocket(handle, FIONBIO, &enabled) == 0;
    }
#else
    using Handle                    = int;
    constexpr Handle kInvalidHandle = -1;

    void start_sockets()
    {
    }
    int last_error()
    {
        return errno;
    }
    bool would_block(int error)
    {
        return error == EAGAIN || error == EWOULDBLOCK;
    }
    bool connect_pending(int error)
    {
        return error == EINPROGRESS;
    }
    bool timed_out(int error)
    {
        return error == ETIMEDOUT;
    }
    void close_handle(Handle handle)
    {
        ::close(handle);
    }
    bool make_non_blocking(Handle handle)
    {
        const int flags = ::fcntl(handle, F_GETFL, 0);
        return flags >= 0 && ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
    }
#endif

#ifdef MSG_NOSIGNAL
    // Linux: a send to a connection the server closed must fail, not raise SIGPIPE.
    constexpr int kSendFlags = MSG_NOSIGNAL;
#else
    constexpr int kSendFlags = 0;
#endif

    Handle to_handle(std::intptr_t value)
    {
        return static_cast<Handle>(value);
    }

    sockaddr_in to_sockaddr(const brick::interfaces::network::Endpoint& endpoint)
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port   = htons(endpoint.port);
        std::memcpy(&address.sin_addr, endpoint.address.octets.data(), endpoint.address.octets.size());
        return address;
    }

    Ipv4Address from_sockaddr(const sockaddr_in& address)
    {
        Ipv4Address result;
        std::memcpy(result.octets.data(), &address.sin_addr, result.octets.size());
        return result;
    }
}  // namespace

// ---- PcHostNetwork ----------------------------------------------------------

Status PcHostNetwork::begin()
{
    state_ = LinkState::down;
    if (offline_)
        return Status::ok;
    start_sockets();
    // Connecting a UDP socket sends nothing; it only picks the route, whose
    // local address is the PC's address on the internet-facing network.
    const Handle handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == kInvalidHandle)
        return Status::ok;
    const sockaddr_in public_dns = to_sockaddr({ { { 8, 8, 8, 8 } }, 53 });
    sockaddr_in       local{};
    socklen_t         length = sizeof(local);
    if (::connect(handle, reinterpret_cast<const sockaddr*>(&public_dns), sizeof(public_dns)) == 0
        && ::getsockname(handle, reinterpret_cast<sockaddr*>(&local), &length) == 0)
    {
        info_         = {};
        info_.address = from_sockaddr(local);
        state_        = info_.address.unspecified() ? LinkState::down : LinkState::up;
    }
    close_handle(handle);
    return Status::ok;
}

Status PcHostNetwork::ip_info(brick::interfaces::network::IpInfo& info) const
{
    if (state_ != LinkState::up)
        return Status::not_ready;
    info = info_;
    return Status::ok;
}

Status PcHostNetwork::mac_address(brick::interfaces::network::MacAddress& mac) const
{
    mac = { { 0x02, 0x42, 0xB1, 0xC0, 0x00, 0x03 } };
    return Status::ok;
}

Status PcHostNetwork::set_default_route()
{
    return state_ == LinkState::up ? Status::ok : Status::not_ready;
}

// ---- PcDnsResolver ----------------------------------------------------------

struct PcDnsResolver::Lookup
{
    std::mutex  mutex;
    Status      status = Status::busy;
    Ipv4Address address;
};

PcDnsResolver::PcDnsResolver(bool offline) : offline_(offline)
{
}

PcDnsResolver::~PcDnsResolver() = default;

Status PcDnsResolver::resolve(const char* host_name)
{
    if (host_name == nullptr || host_name[0] == '\0' || std::strlen(host_name) > 253)
        return Status::invalid_argument;
    lookup_ = std::make_shared<Lookup>();
    Ipv4Address literal;
    if (brick::core::network::parse_ipv4(host_name, literal))
    {
        lookup_->address = literal;
        lookup_->status  = Status::ok;
        return Status::ok;
    }
    if (offline_)
    {
        lookup_->status = Status::not_found;
        return Status::ok;
    }
    start_sockets();
    // The thread keeps the lookup alive; a cancelled lookup just finishes unseen.
    std::thread(
        [lookup = lookup_, name = std::string(host_name)]
        {
            addrinfo  hints{};
            addrinfo* found                         = nullptr;
            hints.ai_family                         = AF_INET;
            const int                         error = ::getaddrinfo(name.c_str(), nullptr, &hints, &found);
            const std::lock_guard<std::mutex> lock(lookup->mutex);
            if (error != 0 || found == nullptr)
            {
                lookup->status = Status::not_found;
                return;
            }
            lookup->address = from_sockaddr(*reinterpret_cast<const sockaddr_in*>(found->ai_addr));
            lookup->status  = Status::ok;
            ::freeaddrinfo(found);
        })
        .detach();
    return Status::ok;
}

Status PcDnsResolver::result(Ipv4Address& address)
{
    if (!lookup_)
        return Status::not_ready;
    const std::lock_guard<std::mutex> lock(lookup_->mutex);
    if (lookup_->status == Status::ok)
        address = lookup_->address;
    return lookup_->status;
}

void PcDnsResolver::cancel()
{
    lookup_.reset();
}

// ---- PcTcpSocket ------------------------------------------------------------

Status PcTcpSocket::connect(const brick::interfaces::network::Endpoint& remote)
{
    close();
    if (offline_)
        return fail_(Status::error);
    start_sockets();
    const Handle handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == kInvalidHandle)
        return fail_(Status::no_memory);
    socket_ = static_cast<std::intptr_t>(handle);
    if (!make_non_blocking(handle))
        return fail_(Status::error);
    const sockaddr_in address = to_sockaddr(remote);
    if (::connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
    {
        state_ = State::connected;
        return Status::ok;
    }
    if (!connect_pending(last_error()))
        return fail_(Status::error);
    state_ = State::connecting;
    return Status::ok;
}

Status PcTcpSocket::poll_connect()
{
    switch (state_)
    {
        case State::closed:
            return Status::not_ready;
        case State::failed:
            return failure_;
        case State::connected:
        case State::peer_closed:
            return Status::ok;
        case State::connecting:
            break;
    }
    const Handle handle = to_handle(socket_);
    fd_set       writable;
    fd_set       failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(handle, &writable);
    FD_SET(handle, &failed);
    timeval   no_wait{};
    const int ready = ::select(static_cast<int>(handle) + 1, nullptr, &writable, &failed, &no_wait);
    if (ready == 0)
        return Status::busy;
    int       error  = 0;
    socklen_t length = sizeof(error);
    if (ready < 0 || ::getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0)
        return fail_(Status::error);
    if (error != 0)
        return fail_(timed_out(error) ? Status::timeout : Status::error);
    state_ = State::connected;
    return Status::ok;
}

Status PcTcpSocket::send(const std::uint8_t* data, std::size_t size, std::size_t& sent)
{
    sent = 0;
    if (state_ != State::connected)
        return state_ == State::failed ? failure_ : Status::not_ready;
    if (size == 0)
        return Status::ok;
    const auto written =
        ::send(to_handle(socket_), reinterpret_cast<const char*>(data), static_cast<int>(size), kSendFlags);
    if (written > 0)
    {
        sent = static_cast<std::size_t>(written);
        return Status::ok;
    }
    return would_block(last_error()) ? Status::busy : fail_(Status::error);
}

Status PcTcpSocket::receive(std::uint8_t* data, std::size_t capacity, std::size_t& received)
{
    received = 0;
    if (state_ != State::connected)
        return state_ == State::failed ? failure_ : Status::not_ready;
    if (capacity == 0)
        return Status::invalid_argument;
    const auto bytes_read = ::recv(to_handle(socket_), reinterpret_cast<char*>(data), static_cast<int>(capacity), 0);
    if (bytes_read > 0)
    {
        received = static_cast<std::size_t>(bytes_read);
        return Status::ok;
    }
    if (bytes_read == 0)
    {
        state_ = State::peer_closed;
        return Status::not_ready;
    }
    return would_block(last_error()) ? Status::no_data : fail_(Status::error);
}

void PcTcpSocket::close()
{
    if (socket_ != -1)
        close_handle(to_handle(socket_));
    socket_ = -1;
    state_  = State::closed;
}

Status PcTcpSocket::fail_(Status status)
{
    close();
    state_   = State::failed;
    failure_ = status;
    return status;
}

}  // namespace brick::platform::pc
