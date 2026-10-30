#pragma once

#include <cstdarg>
#include <cstdio>

#include "brick/interfaces/logging/ILogger.h"

namespace brick::platform::pc
{

/// Writes log lines to stdout (info/debug) or stderr (warning/error).
class PcLogger final : public brick::interfaces::logging::ILogger
{
public:
    explicit PcLogger(bool debug = false) : debug_(debug) {}

    void write(brick::interfaces::logging::Level level, const char* tag, const char* format, va_list args) override
    {
        using brick::interfaces::logging::Level;
        if (level == Level::debug && !debug_)
            return;
        FILE* stream = (level == Level::warning || level == Level::error) ? stderr : stdout;
        std::fprintf(stream, "%c (%s) ", letter(level), tag);
        std::vfprintf(stream, format, args);
        std::fputc('\n', stream);
        std::fflush(stream);
    }

private:
    static char letter(brick::interfaces::logging::Level level)
    {
        switch (level)
        {
            case brick::interfaces::logging::Level::info:
                return 'I';
            case brick::interfaces::logging::Level::warning:
                return 'W';
            case brick::interfaces::logging::Level::error:
                return 'E';
            case brick::interfaces::logging::Level::debug:
                return 'D';
        }
        return '?';
    }

    bool debug_;
};

}  // namespace brick::platform::pc
