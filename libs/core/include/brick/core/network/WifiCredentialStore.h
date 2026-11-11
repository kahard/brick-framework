#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/network/WifiTypes.h"
#include "brick/interfaces/storage/IByteStorage.h"

namespace brick::core::network
{

/// The Wi-Fi network chosen on the device (e.g. on its setup page), kept in
/// byte storage (EEPROM, NVS, a file) across restarts as one checked record
/// of kRecordBytes at address. The record can also say "no network": the
/// user forgot it and wants the hotspot.
class WifiCredentialStore final
{
public:
    struct Record
    {
        std::uint32_t magic                                                     = 0;
        char          ssid[interfaces::network::kWifiSsidMaxLength + 1]         = {};
        char          password[interfaces::network::kWifiPasswordMaxLength + 1] = {};
        std::uint8_t  hidden                                                    = 0;
        std::uint8_t  reserved[2]                                               = {};
        std::uint32_t check                                                     = 0;
    };
    static constexpr std::size_t kRecordBytes = sizeof(Record);

    explicit WifiCredentialStore(interfaces::storage::IByteStorage& storage, std::uint32_t address = 0);

    /// Reads the record. Status::not_found when none was ever written or it
    /// is damaged; then stored() is false.
    Status load();
    /// Writes the network (checked like WifiManager does: SSID 1..32,
    /// password empty or 8..63 characters).
    Status save(const interfaces::network::WifiCredentials& credentials);
    /// Writes the "no network" record.
    Status forget();

    /// A record was read or written.
    bool stored() const { return stored_; }
    /// The stored network; nullptr without a record or for "no network".
    /// Its strings live in this object (do not copy it).
    const interfaces::network::WifiCredentials* network() const;

    WifiCredentialStore(const WifiCredentialStore&)            = delete;
    WifiCredentialStore& operator=(const WifiCredentialStore&) = delete;

private:
    Status write_();
    /// Points credentials_ at the record's strings.
    void refresh_();

    interfaces::storage::IByteStorage&   storage_;
    std::uint32_t                        address_;
    Record                               record_{};
    interfaces::network::WifiCredentials credentials_{};
    bool                                 stored_ = false;
};

}  // namespace brick::core::network
