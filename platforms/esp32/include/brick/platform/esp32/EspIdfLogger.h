#pragma once
#include "brick/interfaces/logging/ILogger.h"
namespace brick::platform::esp32 {
class EspIdfLogger final : public brick::interfaces::logging::ILogger {
public:
    explicit EspIdfLogger(int minimum_level = 0) : minimum_level_(minimum_level) {}
    void write(brick::interfaces::logging::Level level, const char* tag, const char* format, va_list args) override;
private: int minimum_level_;
};
class NullLogger final : public brick::interfaces::logging::ILogger {
public: void write(brick::interfaces::logging::Level, const char*, const char*, va_list) override {}
};
}
