#pragma once

#include <cstdlib>
#include <optional>
#include <utility>

#include "brick/Status.h"

namespace brick::boards::common
{

/// Storage for an optional board peripheral selected by a Features flag.
///
/// An enabled peripheral is constructed with emplace() in the board
/// constructor. A disabled one takes no memory, begin() reports success and
/// any attempt to access it fails at compile time with a readable message.
template <bool Enabled, typename T>
class Peripheral;

template <typename T>
class Peripheral<true, T> final
{
public:
    template <typename... Args>
    void emplace(Args&&... args)
    {
        value_.emplace(std::forward<Args>(args)...);
    }

    Status begin() { return value_.has_value() ? value_->begin() : Status::not_ready; }
    T&     get() { return *value_; }

private:
    std::optional<T> value_;
};

template <typename T>
class Peripheral<false, T> final
{
public:
    template <typename... Args>
    void emplace(Args&&...)
    {
    }

    Status begin() { return Status::ok; }

    template <bool Enabled = false>
    T& get()
    {
        static_assert(Enabled, "This peripheral is disabled in the board Features; enable it before accessing it");
        std::abort();
    }
};

}  // namespace brick::boards::common
