#pragma once
#include <cstdarg>

namespace brick::interfaces::logging {
enum class Level { info, warning, error, debug };
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void write(Level level, const char* tag, const char* format, va_list args) = 0;
    void info(const char* tag, const char* format, ...) { va_list a; va_start(a, format); write(Level::info, tag, format, a); va_end(a); }
    void warning(const char* tag, const char* format, ...) { va_list a; va_start(a, format); write(Level::warning, tag, format, a); va_end(a); }
    void error(const char* tag, const char* format, ...) { va_list a; va_start(a, format); write(Level::error, tag, format, a); va_end(a); }
    void debug(const char* tag, const char* format, ...) { va_list a; va_start(a, format); write(Level::debug, tag, format, a); va_end(a); }
};
}
