#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/system/IFirmwareUpdate.h"
#include "esp_ota_ops.h"

namespace brick::platform::esp32
{

/// This ESP32's own program, replaced by a new one (ESP-IDF's app_update):
/// the image goes into the program slot not in use as it comes (each flash
/// sector erased when the image reaches it, so no call takes longer than
/// about 50 ms), is checked at finish() and starts at the next restart. The
/// partition table needs two program slots (ota_0, ota_1) and their choice
/// (otadata).
///
/// With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE a new program is on trial until
/// it calls confirm(): one that restarts before that (it hangs, a watchdog
/// fires, power is cycled) is replaced by the program before it.
class EspFirmwareUpdate final : public interfaces::system::IFirmwareUpdate
{
public:
    /// not_supported: no second program slot; no_memory: the image is larger
    /// than the slot.
    Status begin(std::uint32_t size) override;
    /// Takes all of it.
    Status write(const std::uint8_t* data, std::size_t size, std::size_t& written) override;
    /// Reads the image back and checks it (a few hundred milliseconds).
    Status finish() override;
    void   abort() override;
    /// Does not return.
    Status restart() override;
    /// "<version> <build date> <build time> <first four bytes of its hash>"
    /// of the program in use, as its image has them (esp_app_desc_t).
    const char* description() const override;

    /// The program in use was started for the first time after an update and
    /// has not been confirmed.
    bool on_trial() const;
    /// The program in use stays.
    Status confirm();
    /// Label of the slot the program in use is in ("ota_0", "factory").
    const char* slot() const;

private:
    static constexpr std::size_t kDescriptionBytes = 96;

    esp_ota_handle_t       handle_  = 0;
    const esp_partition_t* target_  = nullptr;
    std::uint32_t          size_    = 0;
    std::uint32_t          written_ = 0;
    bool                   running_ = false;
    mutable char           description_[kDescriptionBytes]{};
};

}  // namespace brick::platform::esp32
