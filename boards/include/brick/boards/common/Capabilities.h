#pragma once

#include <cstdint>

#include "brick/interfaces/board/BoardDescriptor.h"

namespace brick::boards::common
{

/// Returns the capability bit when the matching feature is compiled in.
constexpr std::uint32_t capability_if(bool enabled, brick::interfaces::board::Capability capability)
{
    return enabled ? static_cast<std::uint32_t>(capability) : 0U;
}

}  // namespace brick::boards::common
