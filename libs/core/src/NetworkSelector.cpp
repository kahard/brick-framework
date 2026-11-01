#include "brick/core/network/NetworkSelector.h"

namespace brick::core::network
{

using interfaces::network::LinkState;

Status NetworkSelector::update()
{
    interfaces::network::INetworkInterface* chosen = nullptr;
    for (std::size_t index = 0; index < count_ && chosen == nullptr; ++index)
        if (interfaces_[index] != nullptr && interfaces_[index]->link_state() == LinkState::up)
            chosen = interfaces_[index];
    if (chosen == active_)
        return Status::ok;
    active_ = chosen;
    return active_ == nullptr ? Status::ok : active_->set_default_route();
}

const char* NetworkSelector::name() const
{
    return active_ == nullptr ? "none" : active_->name();
}

LinkState NetworkSelector::link_state() const
{
    if (active_ != nullptr && active_->link_state() == LinkState::up)
        return LinkState::up;
    for (std::size_t index = 0; index < count_; ++index)
        if (interfaces_[index] != nullptr && interfaces_[index]->link_state() != LinkState::down)
            return LinkState::connecting;
    return LinkState::down;
}

Status NetworkSelector::ip_info(interfaces::network::IpInfo& info) const
{
    return active_ == nullptr ? Status::not_ready : active_->ip_info(info);
}

Status NetworkSelector::mac_address(interfaces::network::MacAddress& mac) const
{
    return active_ == nullptr ? Status::not_ready : active_->mac_address(mac);
}

Status NetworkSelector::set_default_route()
{
    return active_ == nullptr ? Status::not_ready : active_->set_default_route();
}

}  // namespace brick::core::network
