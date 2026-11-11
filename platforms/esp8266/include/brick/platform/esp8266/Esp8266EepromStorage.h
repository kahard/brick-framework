#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/interfaces/storage/IByteStorage.h"

namespace brick::platform::esp8266
{

/// IByteStorage over the Arduino EEPROM library: size bytes kept in RAM and
/// in the flash sector the core reserves for EEPROM. write() commits the
/// sector (an erase and a program, tens of milliseconds), so it suits
/// settings, not logs.
class Esp8266EepromStorage final : public brick::interfaces::storage::IByteStorage
{
public:
    /// size: 4..4096 bytes.
    explicit Esp8266EepromStorage(std::size_t size = 256) : size_(size) {}

    /// Reads the sector into RAM; read() and write() call it when needed.
    Status begin();
    Status read(std::uint32_t address, void* data, std::size_t size) override;
    Status write(std::uint32_t address, const void* data, std::size_t size) override;

private:
    bool fits_(std::uint32_t address, std::size_t size) const;

    std::size_t size_;
    bool        begun_ = false;
};

}  // namespace brick::platform::esp8266
