#pragma once

#include <cstddef>
#include <cstdint>

namespace brick::core::audio
{

/// 8-bit unsigned mono samples at a sample rate. The buffer only refers to
/// the samples: their owner keeps them valid while the buffer, or a player
/// playing it, uses them.
class AudioBuffer
{
public:
    AudioBuffer() = default;
    AudioBuffer(const std::uint8_t* data, std::size_t size, std::uint32_t sample_rate);

    const std::uint8_t* data() const;
    std::size_t         size() const;
    std::uint32_t       sample_rate() const;
    bool                empty() const;

    void clear();
    void assign(const std::uint8_t* data, std::size_t size, std::uint32_t sample_rate);

private:
    const std::uint8_t* data_        = nullptr;
    std::size_t         size_        = 0;
    std::uint32_t       sample_rate_ = 0;
};

}  // namespace brick::core::audio
