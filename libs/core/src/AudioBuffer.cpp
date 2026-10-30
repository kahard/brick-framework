#include "brick/core/audio/AudioBuffer.h"

namespace brick::core::audio
{

AudioBuffer::AudioBuffer(const std::uint8_t* data, std::size_t size, std::uint32_t sample_rate)
{
    assign(data, size, sample_rate);
}

const std::uint8_t* AudioBuffer::data() const
{
    return data_;
}

std::size_t AudioBuffer::size() const
{
    return size_;
}

std::uint32_t AudioBuffer::sample_rate() const
{
    return sample_rate_;
}

bool AudioBuffer::empty() const
{
    return size_ == 0;
}

void AudioBuffer::clear()
{
    data_        = nullptr;
    size_        = 0;
    sample_rate_ = 0;
}

void AudioBuffer::assign(const std::uint8_t* data, std::size_t size, std::uint32_t sample_rate)
{
    data_        = data;
    size_        = data != nullptr ? size : 0;
    sample_rate_ = sample_rate;
}

}  // namespace brick::core::audio
