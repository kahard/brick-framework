#pragma once

#include "brick/platform/esp32/p4/SdmmcFileSystem.h"

namespace brick::platform::esp32::p4::jc8012::detail
{

inline SdmmcFileSystemConfig jc8012_sdmmc()
{
    return {
        .clk = GPIO_NUM_43,
        .cmd = GPIO_NUM_44,
        .d0 = GPIO_NUM_39,
        .d1 = GPIO_NUM_40,
        .d2 = GPIO_NUM_41,
        .d3 = GPIO_NUM_42,
        .max_freq_khz = 10'000,
        .bus_width = 4,
        .host_slot = SDMMC_HOST_SLOT_0,
    };
}

} // namespace brick::platform::esp32::p4::jc8012::detail
