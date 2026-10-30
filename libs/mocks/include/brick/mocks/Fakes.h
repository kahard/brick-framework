#pragma once

// Test doubles for the BRICK interfaces. They record what code under test did
// and let tests script inputs; unlike the PC simulator they do not emulate
// hardware. Host-only code: may use the standard library freely.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/audio/IAudioOutput.h"
#include "brick/interfaces/display/AssetDescriptor.h"
#include "brick/interfaces/display/IAssetReader.h"
#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/interfaces/display/ITouchscreen.h"
#include "brick/interfaces/logging/ILogger.h"
#include "brick/interfaces/storage/IFileSystem.h"
#include "brick/interfaces/time/ITimeProvider.h"
#include "brick/interfaces/timing/IClock.h"

namespace brick::mocks
{

/// Clock and time provider whose time only moves when the test sets it.
class FakeClock final : public brick::interfaces::timing::IClock, public brick::interfaces::time::ITimeProvider
{
public:
    std::uint32_t now = 0;
    std::uint32_t millis() const override { return now; }
    void          delay_ms(std::uint32_t milliseconds) override { now += milliseconds; }
};

/// Read-only file over a byte vector owned by the test.
class MemoryFile final : public brick::interfaces::storage::IFile
{
public:
    explicit MemoryFile(const std::vector<std::uint8_t>& data) : data_(data) {}

    std::size_t read(void* buffer, std::size_t size, std::size_t count) override
    {
        const std::size_t requested = size * count;
        const std::size_t available = data_.size() - position_;
        const std::size_t bytes     = requested < available ? requested : available;
        if (bytes > 0)
        {
            std::memcpy(buffer, data_.data() + position_, bytes);
            position_ += bytes;
        }
        return size == 0 ? 0 : bytes / size;
    }

    bool seek(long offset, int origin) override
    {
        long base = 0;
        if (origin == SEEK_CUR)
            base = static_cast<long>(position_);
        if (origin == SEEK_END)
            base = static_cast<long>(data_.size());
        const long target = base + offset;
        if (target < 0 || target > static_cast<long>(data_.size()))
            return false;
        position_ = static_cast<std::size_t>(target);
        return true;
    }

private:
    const std::vector<std::uint8_t>& data_;
    std::size_t                      position_ = 0;
};

/// File system with files kept in memory; list_files() returns their keys.
class MemoryFileSystem final : public brick::interfaces::storage::IFileSystem
{
public:
    bool mount() override { return mountable; }

    std::size_t list_files(const char*, brick::interfaces::storage::FileEntry* entries, std::size_t capacity) override
    {
        std::size_t count = 0;
        for (const auto& file : files)
        {
            if (entries != nullptr && count < capacity)
                entries[count].set_name(file.first.c_str());
            ++count;
        }
        return count;
    }

    bool open(const char* path, const char*, brick::interfaces::storage::FileHandle& file) override
    {
        file.close();
        const auto it = files.find(path);
        if (it == files.end())
            return false;
        open_.push_back(std::make_unique<MemoryFile>(it->second));
        file.attach(*this, *open_.back());
        return true;
    }

    /// Files opened and not closed yet.
    std::size_t open_files() const { return open_.size(); }

    bool                                             mountable = true;
    std::map<std::string, std::vector<std::uint8_t>> files;

protected:
    void close(brick::interfaces::storage::IFile& file) override
    {
        for (auto it = open_.begin(); it != open_.end(); ++it)
            if (it->get() == &file)
            {
                open_.erase(it);
                return;
            }
    }

private:
    std::vector<std::unique_ptr<MemoryFile>> open_;
};

/// Partial-update display that records every transfer.
class RecordingDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit RecordingDisplay(brick::interfaces::display::DisplaySize display_size = { 320, 240 }) : size_(display_size)
    {
    }

    bool                                    begin() override { return true; }
    brick::interfaces::display::DisplaySize size() const override { return size_; }
    brick::interfaces::display::PixelFormat pixel_format() const override
    {
        return brick::interfaces::display::PixelFormat::rgb565;
    }
    bool set_rotation(brick::interfaces::display::Rotation) override { return true; }

    bool draw_buffer(brick::interfaces::display::DisplayRect        area,
                     const brick::interfaces::display::PixelBuffer& buffer) override
    {
        if (!buffer.valid() || buffer.format != pixel_format()
            || buffer.stride_bytes != static_cast<std::size_t>(area.width) * 2)
            return false;
        last_area       = area;
        last_pixels     = buffer.data;
        last_byte_count = buffer.stride_bytes * buffer.height;
        submitted_areas.push_back(area);
        submitted_byte_counts.push_back(last_byte_count);
        return true;
    }

    brick::interfaces::display::DisplayRect              last_area{};
    const std::uint8_t*                                  last_pixels     = nullptr;
    std::size_t                                          last_byte_count = 0;
    std::vector<brick::interfaces::display::DisplayRect> submitted_areas;
    std::vector<std::size_t>                             submitted_byte_counts;

private:
    brick::interfaces::display::DisplaySize size_;
};

/// Touch screen that returns queued samples, one per read().
class ScriptedTouchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    bool begin() override { return true; }

    bool read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override
    {
        count = 0;
        if (next_ >= samples.size())
            return false;
        const auto& sample = samples[next_++];
        for (; count < sample.size() && count < capacity; ++count)
            points[count] = sample[count];
        return true;
    }

    std::vector<std::vector<brick::interfaces::display::TouchPoint>> samples;

private:
    std::size_t next_ = 0;
};

/// Asset reader over bytes owned by the test; records each read.
class MemoryAssetReader final : public brick::interfaces::display::IAssetReader
{
public:
    explicit MemoryAssetReader(const std::vector<std::uint8_t>& data) : data_(data) {}

    bool read(const brick::interfaces::display::ImageAsset& asset, std::size_t offset, std::uint8_t* destination,
              std::size_t bytes) override
    {
        if (destination == nullptr || asset.data != data_.data() || offset + bytes > data_.size())
            return false;
        std::memcpy(destination, data_.data() + offset, bytes);
        offsets.push_back(offset);
        sizes.push_back(bytes);
        return true;
    }

    const std::vector<std::uint8_t>& data_;
    std::vector<std::size_t>         offsets;
    std::vector<std::size_t>         sizes;
};

/// Asset source over a bundle owned by the test; records each read.
class MemoryAssetSource final : public brick::interfaces::display::IAssetSource
{
public:
    explicit MemoryAssetSource(const std::vector<std::uint8_t>& data) : data_(data) {}

    bool read(const brick::interfaces::display::AssetDescriptor& asset, std::size_t offset, std::uint8_t* destination,
              std::size_t bytes) override
    {
        if (destination == nullptr || offset > asset.size || bytes > asset.size - offset || asset.offset > data_.size()
            || bytes > data_.size() - asset.offset - offset)
            return false;
        std::memcpy(destination, data_.data() + asset.offset + offset, bytes);
        offsets.push_back(offset);
        sizes.push_back(bytes);
        return true;
    }

    const std::vector<std::uint8_t>& data_;
    std::vector<std::size_t>         offsets;
    std::vector<std::size_t>         sizes;
};

/// Audio output that keeps every sample.
class FakeAudioOutput final : public brick::interfaces::audio::IAudioOutput
{
public:
    bool begin() override
    {
        started = true;
        return true;
    }
    void write_sample(std::uint8_t sample) override { samples.push_back(sample); }
    void stop() override { stopped = true; }

    bool                      started = false;
    bool                      stopped = false;
    std::vector<std::uint8_t> samples;
};

/// Logger that keeps formatted lines, for asserting on diagnostics.
class RecordingLogger final : public brick::interfaces::logging::ILogger
{
public:
    void write(brick::interfaces::logging::Level level, const char* tag, const char* format, va_list args) override
    {
        char text[256];
        std::vsnprintf(text, sizeof(text), format, args);
        lines.push_back({ level, tag != nullptr ? tag : "", text });
    }

    struct Line
    {
        brick::interfaces::logging::Level level;
        std::string                       tag;
        std::string                       text;
    };
    std::vector<Line> lines;
};

}  // namespace brick::mocks
