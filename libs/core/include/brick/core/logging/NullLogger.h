#pragma once

#include <cstdarg>

#include "brick/interfaces/logging/ILogger.h"

namespace brick::core::logging
{

/// Logger for builds with logging compiled out.
class NullLogger final : public brick::interfaces::logging::ILogger
{
public:
    void write(brick::interfaces::logging::Level, const char*, const char*, va_list) override {}
};

}  // namespace brick::core::logging
