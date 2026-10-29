#pragma once

#include "brick/interfaces/logging/ILogger.h"
#include "brick/platform/esp32/EspIdfLogger.h"

namespace brick::boards::esp32::detail
{

template <bool Enabled> class Logger;

template <> class Logger<true>
{
  public:
    explicit Logger(int level) : value_(level) {}

    brick::interfaces::logging::ILogger& get() { return value_; }

  private:
    brick::platform::esp32::EspIdfLogger value_;
};

template <> class Logger<false>
{
  public:
    explicit Logger(int) {}

    brick::interfaces::logging::ILogger& get() { return value_; }

  private:
    brick::platform::esp32::NullLogger value_;
};

} // namespace brick::boards::esp32::detail
