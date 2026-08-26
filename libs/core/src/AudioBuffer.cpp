#include "brick/core/audio/AudioBuffer.h"

#include <utility>

namespace brick::core::audio
{

AudioBuffer::AudioBuffer() = default;

AudioBuffer::AudioBuffer(std::vector<std::uint8_t> samples, std::uint32_t sample_rate) : samples_(std::move(samples)), sample_rate_(sample_rate)
{
}

const std::uint8_t* AudioBuffer::data() const
{
    return view_data_ != nullptr ? view_data_ : samples_.data();
}

std::size_t AudioBuffer::size() const
{
    return view_data_ != nullptr ? view_size_ : samples_.size();
}

std::uint32_t AudioBuffer::sample_rate() const
{
    return sample_rate_;
}

bool AudioBuffer::empty() const
{
    return size() == 0;
}

void AudioBuffer::clear()
{
    view_data_ = nullptr;
    view_size_ = 0;
    samples_.clear();
    sample_rate_ = 0;
}

void AudioBuffer::assign(std::vector<std::uint8_t> samples, std::uint32_t sample_rate)
{
    view_data_   = nullptr;
    view_size_   = 0;
    samples_     = std::move(samples);
    sample_rate_ = sample_rate;
}

void AudioBuffer::assign_view(const std::uint8_t* data, std::size_t size, std::uint32_t sample_rate)
{
    samples_.clear();
    view_data_   = data;
    view_size_   = size;
    sample_rate_ = sample_rate;
}

}  // namespace brick::core::audio
