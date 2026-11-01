#pragma once

// Socket calls that differ between Winsock and POSIX, shared by the PC
// network classes (PcSockets.cpp, WebScreen.cpp). Internal to platforms/pc.

#include <cstdint>
#include <mutex>

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

namespace brick::platform::pc::sockets
{

#ifdef _WIN32
using Handle                           = SOCKET;
inline constexpr Handle kInvalidHandle = INVALID_SOCKET;

inline void start()
{
    static std::once_flag started;
    std::call_once(started,
                   []
                   {
                       WSADATA data;
                       WSAStartup(MAKEWORD(2, 2), &data);
                   });
}
inline int last_error()
{
    return WSAGetLastError();
}
inline bool would_block(int error)
{
    return error == WSAEWOULDBLOCK;
}
inline bool connect_pending(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}
inline bool timed_out(int error)
{
    return error == WSAETIMEDOUT;
}
inline void close_handle(Handle handle)
{
    closesocket(handle);
}
inline bool make_non_blocking(Handle handle)
{
    u_long enabled = 1;
    return ioctlsocket(handle, FIONBIO, &enabled) == 0;
}
#else
using Handle                           = int;
inline constexpr Handle kInvalidHandle = -1;

inline void start()
{
}
inline int last_error()
{
    return errno;
}
inline bool would_block(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK;
}
inline bool connect_pending(int error)
{
    return error == EINPROGRESS;
}
inline bool timed_out(int error)
{
    return error == ETIMEDOUT;
}
inline void close_handle(Handle handle)
{
    ::close(handle);
}
inline bool make_non_blocking(Handle handle)
{
    const int flags = ::fcntl(handle, F_GETFL, 0);
    return flags >= 0 && ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

#ifdef MSG_NOSIGNAL
// Linux: a send to a connection the other side closed must fail, not raise SIGPIPE.
inline constexpr int kSendFlags = MSG_NOSIGNAL;
#else
inline constexpr int kSendFlags = 0;
#endif

inline Handle to_handle(std::intptr_t value)
{
    return static_cast<Handle>(value);
}

}  // namespace brick::platform::pc::sockets
