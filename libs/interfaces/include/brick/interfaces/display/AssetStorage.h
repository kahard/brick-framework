#pragma once

namespace brick::interfaces::display {

enum class AssetStorage {
    flash_partition,
    psram_cache,
    usb_or_sd,
};

} // namespace brick::interfaces::display
