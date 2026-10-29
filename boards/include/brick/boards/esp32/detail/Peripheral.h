#pragma once

#include <optional>
#include <type_traits>
#include <utility>

namespace brick::boards::esp32::detail
{

template <typename T> class EnabledPeripheral
{
  public:
    template <typename... Args> void emplace(Args&&... args) { value_.emplace(std::forward<Args>(args)...); }

    bool begin() { return value_.has_value() && value_->begin(); }
    T& get() { return *value_; }

  private:
    std::optional<T> value_;
};

template <typename T> class DisabledPeripheral
{
  public:
    template <typename... Args> void emplace(Args&&...) {}

    bool begin() { return true; }
};

template <bool Enabled, typename T>
using Peripheral = std::conditional_t<Enabled, EnabledPeripheral<T>, DisabledPeripheral<T>>;

} // namespace brick::boards::esp32::detail
