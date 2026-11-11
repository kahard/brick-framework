#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/storage/IByteStorage.h"

namespace brick::platform::esp32
{

/// IByteStorage kept as one NVS blob (key in a namespace of the default NVS
/// partition) of size bytes (up to kMaxSize), blank (0xFF) until written. A
/// copy stays in RAM; write() stores the whole blob and commits. Call it
/// after the NVS is started (EspWifi::begin() does that).
class EspNvsStorage final : public brick::interfaces::storage::IByteStorage
{
public:
    static constexpr std::size_t kMaxSize = 256;

    /// name_space and key: up to 15 characters, kept as pointers.
    EspNvsStorage(const char* name_space, const char* key, std::size_t size = kMaxSize);

    /// Reads the blob; read() and write() call it when needed.
    Status begin();
    Status read(std::uint32_t address, void* data, std::size_t size) override;
    Status write(std::uint32_t address, const void* data, std::size_t size) override;

private:
    bool fits_(std::uint32_t address, std::size_t size) const;

    const char*                        name_space_;
    const char*                        key_;
    std::size_t                        size_;
    std::array<std::uint8_t, kMaxSize> bytes_{};
    bool                               begun_ = false;
};

}  // namespace brick::platform::esp32
