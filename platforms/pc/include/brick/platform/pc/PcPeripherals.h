#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "brick/Status.h"
#include "brick/interfaces/audio/IBuzzer.h"
#include "brick/interfaces/storage/IByteStorage.h"
#include "brick/interfaces/storage/INorFlash.h"

namespace brick::platform::pc
{

/// Buzzer without sound: counts the beeps an application makes.
class PcBuzzer final : public brick::interfaces::audio::IBuzzer
{
public:
    Status begin() override { return Status::ok; }

    Status set_enabled(bool enabled) override
    {
        if (enabled && !enabled_)
            ++beeps_;
        enabled_ = enabled;
        return Status::ok;
    }

    Status beep(std::uint16_t) override
    {
        ++beeps_;
        return Status::ok;
    }

    bool          enabled() const { return enabled_; }
    std::uint32_t beeps() const { return beeps_; }

private:
    bool          enabled_ = false;
    std::uint32_t beeps_   = 0;
};

/// EEPROM-like byte storage in memory, blank (0xFF) at start.
class PcByteStorage final : public brick::interfaces::storage::IByteStorage
{
public:
    explicit PcByteStorage(std::size_t capacity) : bytes_(capacity, 0xFF) {}

    Status read(std::uint32_t address, void* data, std::size_t size) override
    {
        if (data == nullptr || !fits(address, size))
            return Status::invalid_argument;
        std::memcpy(data, bytes_.data() + address, size);
        return Status::ok;
    }

    Status write(std::uint32_t address, const void* data, std::size_t size) override
    {
        if (data == nullptr || !fits(address, size))
            return Status::invalid_argument;
        std::memcpy(bytes_.data() + address, data, size);
        return Status::ok;
    }

private:
    bool fits(std::uint32_t address, std::size_t size) const
    {
        return address <= bytes_.size() && size <= bytes_.size() - address;
    }

    std::vector<std::uint8_t> bytes_;
};

/// Blank NOR flash in memory that answers with a fixed JEDEC ID.
class PcNorFlash final : public brick::interfaces::storage::INorFlash
{
public:
    PcNorFlash(brick::interfaces::storage::JedecId id, std::size_t capacity) : id_(id), bytes_(capacity, 0xFF) {}

    Status read_id(brick::interfaces::storage::JedecId& id) override
    {
        id = id_;
        return Status::ok;
    }

    Status read(std::uint32_t address, void* data, std::size_t size) override
    {
        if (data == nullptr || address > bytes_.size() || size > bytes_.size() - address)
            return Status::invalid_argument;
        std::memcpy(data, bytes_.data() + address, size);
        return Status::ok;
    }

private:
    brick::interfaces::storage::JedecId id_;
    std::vector<std::uint8_t>           bytes_;
};

}  // namespace brick::platform::pc
