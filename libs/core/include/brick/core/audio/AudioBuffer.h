#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace brick::core::audio
{

class AudioBuffer
{
public:
    AudioBuffer();
    AudioBuffer(std::vector<std::uint8_t> samples, std::uint32_t sample_rate);

    const std::uint8_t* data() const;
    std::size_t         size() const;
    std::uint32_t       sample_rate() const;
    bool                empty() const;

    void clear();
    void assign(std::vector<std::uint8_t> samples, std::uint32_t sample_rate);

    // Reference an externally owned sample buffer without copying it. The
    // caller must keep the memory valid until clear() or another assign().
    void assign_view(const std::uint8_t* data, std::size_t size, std::uint32_t sample_rate);

private:
    std::vector<std::uint8_t> samples_;
    const std::uint8_t*       view_data_   = nullptr;
    std::size_t               view_size_   = 0;
    std::uint32_t             sample_rate_ = 0;
};

}  // namespace brick::core::audio
