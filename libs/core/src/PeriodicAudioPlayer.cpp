#include "brick/core/audio/PeriodicAudioPlayer.h"

namespace brick::core::audio
{

PeriodicAudioPlayer::PeriodicAudioPlayer(interfaces::audio::IAudioOutput& output) : output_(output)
{
}

Status PeriodicAudioPlayer::begin()
{
    return output_.begin();
}

Status PeriodicAudioPlayer::play(const AudioBuffer& buffer)
{
    if (buffer.empty())
        return Status::invalid_argument;
    if (const Status status = output_.begin(); !is_ok(status))
        return status;
    current_ = &buffer;
    index_   = 0;
    playing_ = true;
    return Status::ok;
}

void PeriodicAudioPlayer::tick()
{
    if (!playing_ || current_ == nullptr)
    {
        return;
    }
    output_.write_sample(current_->data()[index_++]);
    if (index_ >= current_->size())
    {
        stop();
    }
}

void PeriodicAudioPlayer::stop()
{
    if (!playing_)
    {
        return;
    }
    playing_ = false;
    current_ = nullptr;
    index_   = 0;
    output_.write_sample(128);
    output_.stop();
}

bool PeriodicAudioPlayer::playing() const
{
    return playing_;
}

}  // namespace brick::core::audio
