#pragma once

#include <type_traits>

// Feature flags added after a board was introduced. They default to off, so
// a project's own Features struct written before them keeps compiling and
// keeps its behaviour; a project enables one by declaring the flag.
namespace brick::boards::common
{

template <typename Features, typename = void>
inline constexpr bool wifi_enabled = false;

template <typename Features>
inline constexpr bool wifi_enabled<Features, std::void_t<decltype(Features::wifi)>> = Features::wifi;

template <typename Features, typename = void>
inline constexpr bool ethernet_enabled = false;

template <typename Features>
inline constexpr bool ethernet_enabled<Features, std::void_t<decltype(Features::ethernet)>> = Features::ethernet;

}  // namespace brick::boards::common
