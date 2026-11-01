#pragma once

#include "brick/interfaces/logging/ILogger.h"

namespace brick::boards::common
{

/// Board logger selected by Features::logging. Impl is constructed with the
/// minimum log level; Null is used when logging is compiled out.
template <bool Enabled, typename Impl, typename Null>
class Logger;

template <typename Impl, typename Null>
class Logger<true, Impl, Null> final
{
public:
    explicit Logger(int level) : value_(level) {}

    brick::interfaces::logging::ILogger& get() { return value_; }

private:
    Impl value_;
};

template <typename Impl, typename Null>
class Logger<false, Impl, Null> final
{
public:
    explicit Logger(int) {}

    brick::interfaces::logging::ILogger& get() { return value_; }

private:
    Null value_;
};

}  // namespace brick::boards::common
