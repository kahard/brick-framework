#include "brick/platform/esp8266/Esp8266EepromStorage.h"

#include <EEPROM.h>

#include <cstring>

namespace brick::platform::esp8266
{

Status Esp8266EepromStorage::begin()
{
    if (begun_)
        return Status::ok;
    if (size_ < 4 || size_ > 4096)
        return Status::invalid_argument;
    EEPROM.begin(size_);
    begun_ = true;
    return Status::ok;
}

Status Esp8266EepromStorage::read(std::uint32_t address, void* data, std::size_t size)
{
    if (data == nullptr || !fits_(address, size))
        return Status::invalid_argument;
    if (const Status status = begin(); !is_ok(status))
        return status;
    std::memcpy(data, EEPROM.getConstDataPtr() + address, size);
    return Status::ok;
}

Status Esp8266EepromStorage::write(std::uint32_t address, const void* data, std::size_t size)
{
    if (data == nullptr || !fits_(address, size))
        return Status::invalid_argument;
    if (const Status status = begin(); !is_ok(status))
        return status;
    std::memcpy(EEPROM.getDataPtr() + address, data, size);
    return EEPROM.commit() ? Status::ok : Status::error;
}

bool Esp8266EepromStorage::fits_(std::uint32_t address, std::size_t size) const
{
    return address <= size_ && size <= size_ - address;
}

}  // namespace brick::platform::esp8266
