#include "brick/core/network/WifiCredentialStore.h"

#include <cstddef>
#include <cstring>

#include "brick/core/network/WifiConfigCheck.h"

namespace brick::core::network
{

namespace
{
    constexpr std::uint32_t kMagic = 0x31465742U;  // "BWF1"

    /// FNV-1a over the record up to its check field.
    std::uint32_t check_of(const WifiCredentialStore::Record& record)
    {
        const auto*   bytes = reinterpret_cast<const std::uint8_t*>(&record);
        std::uint32_t hash  = 2166136261U;
        for (std::size_t index = 0; index < offsetof(WifiCredentialStore::Record, check); ++index)
            hash = (hash ^ bytes[index]) * 16777619U;
        return hash;
    }
}  // namespace

WifiCredentialStore::WifiCredentialStore(interfaces::storage::IByteStorage& storage, std::uint32_t address)
    : storage_(storage), address_(address)
{
}

Status WifiCredentialStore::load()
{
    stored_ = false;
    Record record;
    if (const Status status = storage_.read(address_, &record, sizeof(record)); !is_ok(status))
        return status;
    record.ssid[sizeof(record.ssid) - 1]         = '\0';
    record.password[sizeof(record.password) - 1] = '\0';
    if (record.magic != kMagic || record.check != check_of(record))
        return Status::not_found;
    record_ = record;
    refresh_();
    stored_ = true;
    return Status::ok;
}

Status WifiCredentialStore::save(const interfaces::network::WifiCredentials& credentials)
{
    if (const Status status = check_credentials(credentials); !is_ok(status))
        return status;
    Record record;
    if (const Status status = copy_text(record.ssid, sizeof(record.ssid), credentials.ssid); !is_ok(status))
        return status;
    const char* password = credentials.password == nullptr ? "" : credentials.password;
    if (const Status status = copy_text(record.password, sizeof(record.password), password); !is_ok(status))
        return status;
    record.hidden = credentials.hidden ? 1 : 0;
    record_       = record;
    return write_();
}

Status WifiCredentialStore::forget()
{
    record_ = Record{};
    return write_();
}

const interfaces::network::WifiCredentials* WifiCredentialStore::network() const
{
    return stored_ && record_.ssid[0] != '\0' ? &credentials_ : nullptr;
}

Status WifiCredentialStore::write_()
{
    record_.magic = kMagic;
    record_.check = check_of(record_);
    refresh_();
    stored_ = false;
    if (const Status status = storage_.write(address_, &record_, sizeof(record_)); !is_ok(status))
        return status;
    stored_ = true;
    return Status::ok;
}

void WifiCredentialStore::refresh_()
{
    credentials_.ssid     = record_.ssid;
    credentials_.password = record_.password;
    credentials_.hidden   = record_.hidden != 0;
}

}  // namespace brick::core::network
