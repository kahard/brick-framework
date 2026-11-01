#pragma once

#include <cstdint>

namespace brick
{

/// Result of an operation that can fail. A function returns Status when its
/// result means "did it work"; a function that answers a question
/// (is_pressed(), mounted(), valid()) returns bool.
///
/// [[nodiscard]]: discarding a Status is a compiler warning, an error with
/// -Werror. Compare with Status::ok or use is_ok(); there is no conversion to
/// bool, so `if (!device.begin())` does not compile by accident.
///
/// A code exists only when a caller can react to it differently from the
/// others; everything else is Status::error.
enum class [[nodiscard]] Status : std::uint8_t
{
    ok,                ///< done
    error,             ///< failed, nothing more specific to report (bus or SDK error, corrupt data)
    invalid_argument,  ///< the caller passed something the function cannot accept: a bug in the caller
    not_ready,         ///< not started or not mounted yet: call begin() / mount() first
    busy,              ///< the resource is in use (transfer pending, every file object open): try later
    timeout,           ///< a wait ran out of time: the operation may succeed when repeated
    no_data,           ///< nothing new to read from a polled input: not an error
    not_found,         ///< the file, asset or partition does not exist
    no_memory,         ///< a buffer could not be allocated, or the caller's buffer is too small
    not_supported,     ///< this device, mode or format does not offer the operation
};

constexpr bool is_ok(Status status)
{
    return status == Status::ok;
}

/// Constant name of the code for logs, e.g. "timeout".
constexpr const char* to_string(Status status)
{
    switch (status)
    {
        case Status::ok:
            return "ok";
        case Status::error:
            return "error";
        case Status::invalid_argument:
            return "invalid argument";
        case Status::not_ready:
            return "not ready";
        case Status::busy:
            return "busy";
        case Status::timeout:
            return "timeout";
        case Status::no_data:
            return "no data";
        case Status::not_found:
            return "not found";
        case Status::no_memory:
            return "no memory";
        case Status::not_supported:
            return "not supported";
    }
    return "unknown";
}

}  // namespace brick
