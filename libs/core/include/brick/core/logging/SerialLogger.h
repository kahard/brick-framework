#pragma once

#include <cstdarg>
#include <cstddef>

#include "brick/interfaces/communication/ISerialPort.h"
#include "brick/interfaces/logging/ILogger.h"

namespace brick::core::logging
{

/// Writes "I (tag) message" lines ending in CR LF to a serial port, for
/// boards without a platform logger. A line longer than kLineBytes is cut.
class SerialLogger final : public brick::interfaces::logging::ILogger
{
public:
    static constexpr std::size_t kLineBytes = 128;

    explicit SerialLogger(brick::interfaces::communication::ISerialPort& port, bool debug = false)
        : port_(port), debug_(debug)
    {
    }

    void write(brick::interfaces::logging::Level level, const char* tag, const char* format, va_list args) override;

private:
    brick::interfaces::communication::ISerialPort& port_;
    bool                                           debug_;
};

}  // namespace brick::core::logging
