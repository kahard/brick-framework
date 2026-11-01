#include "brick/core/logging/SerialLogger.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace brick::core::logging
{

namespace
{

    char level_letter(brick::interfaces::logging::Level level)
    {
        using brick::interfaces::logging::Level;
        switch (level)
        {
            case Level::info:
                return 'I';
            case Level::warning:
                return 'W';
            case Level::error:
                return 'E';
            case Level::debug:
                return 'D';
        }
        return '?';
    }

    // snprintf returns the length it wanted to write; clamp it to what fits.
    std::size_t written(int result, std::size_t room)
    {
        return result < 0 ? 0 : std::min(static_cast<std::size_t>(result), room - 1);
    }

}  // namespace

void SerialLogger::write(brick::interfaces::logging::Level level, const char* tag, const char* format, va_list args)
{
    if (level == brick::interfaces::logging::Level::debug && !debug_)
        return;
    // The text ends one byte early, so CR replaces its terminating NUL and LF
    // takes the last byte.
    char              line[kLineBytes];
    const std::size_t text_room = sizeof(line) - 1;
    std::size_t       length = written(std::snprintf(line, text_room, "%c (%s) ", level_letter(level), tag), text_room);
    length += written(std::vsnprintf(line + length, text_room - length, format, args), text_room - length);
    line[length++] = '\r';
    line[length++] = '\n';
    port_.write(reinterpret_cast<const std::uint8_t*>(line), length);
}

}  // namespace brick::core::logging
