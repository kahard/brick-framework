#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "brick/Status.h"
#include "brick/interfaces/audio/IBuzzer.h"
#include "brick/interfaces/audio/IPcmOutput.h"
#include "brick/interfaces/storage/IByteStorage.h"
#include "brick/interfaces/storage/INorFlash.h"
#include "brick/interfaces/time/ITimeProvider.h"

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

/// Speaker without sound: takes frames at the sample rate of the simulated
/// clock, as DMA buffers holding 100 ms would, and counts what it played.
class PcPcmOutput final : public brick::interfaces::audio::IPcmOutput
{
public:
    explicit PcPcmOutput(brick::interfaces::time::ITimeProvider& time) : time_(time) {}

    Status start(const brick::interfaces::audio::PcmFormat& format) override
    {
        if (format.sample_rate == 0 || format.channels == 0 || format.channels > 2)
            return Status::not_supported;
        format_  = format;
        queued_  = 0;
        last_ms_ = time_.millis();
        rest_    = 0;
        ++starts_;
        return Status::ok;
    }
    Status write(const std::int16_t* frames, std::size_t count, std::size_t& written) override
    {
        written = 0;
        if (format_.sample_rate == 0)
            return Status::not_ready;
        drain_();
        const std::size_t capacity = format_.sample_rate / 10;
        const std::size_t room     = capacity > queued_ ? capacity - queued_ : 0;
        written                    = count < room ? count : room;
        for (std::size_t index = 0; index < written * format_.channels; ++index)
        {
            const int magnitude = frames[index] < 0 ? -frames[index] : frames[index];
            peak_               = magnitude > peak_ ? magnitude : peak_;
        }
        queued_ += written;
        played_ += written;
        return written == 0 && count > 0 ? Status::busy : Status::ok;
    }
    Status set_volume(std::uint8_t percent) override
    {
        volume_ = percent > 100 ? 100 : percent;
        return Status::ok;
    }
    std::uint8_t                        volume() const override { return volume_; }
    void                                stop() override { format_.sample_rate = 0; }
    brick::interfaces::audio::PcmFormat format() const override { return format_; }

    /// Frames taken since the simulator started.
    std::uint64_t frames_played() const { return played_; }
    /// Largest sample magnitude taken (0..32768).
    int peak() const { return peak_; }
    int starts() const { return starts_; }

private:
    void drain_()
    {
        const std::uint32_t now     = time_.millis();
        const std::uint64_t elapsed = static_cast<std::uint64_t>(now - last_ms_) * format_.sample_rate + rest_;
        last_ms_                    = now;
        const std::uint64_t frames  = elapsed / 1000;
        rest_                       = static_cast<std::uint32_t>(elapsed % 1000);
        queued_                     = frames >= queued_ ? 0 : queued_ - static_cast<std::size_t>(frames);
    }

    brick::interfaces::time::ITimeProvider& time_;
    brick::interfaces::audio::PcmFormat     format_{ 0, 1 };
    std::uint8_t                            volume_  = 60;
    std::size_t                             queued_  = 0;
    std::uint32_t                           last_ms_ = 0;
    std::uint32_t                           rest_    = 0;
    std::uint64_t                           played_  = 0;
    int                                     peak_    = 0;
    int                                     starts_  = 0;
};

}  // namespace brick::platform::pc
