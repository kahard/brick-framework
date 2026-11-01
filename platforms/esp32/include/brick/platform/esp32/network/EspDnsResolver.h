#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/IDnsResolver.h"

namespace brick::platform::esp32
{

class EspDnsResolver;

/// One lookup handed to lwIP; its answer arrives in the lwIP thread.
struct EspDnsQuery
{
    EspDnsResolver*   owner = nullptr;
    std::uint32_t     id    = 0;
    std::atomic<bool> pending{ false };
};

/// IDnsResolver over the lwIP DNS client, without blocking: the query is
/// started in the lwIP thread and answered there. The resolver must outlive
/// its last lookup (keep it static, like the board). lwIP reports "no such
/// name" and "no answer" alike, as Status::not_found.
class EspDnsResolver final : public interfaces::network::IDnsResolver
{
public:
    static constexpr std::size_t kMaxHostNameLength = 253;

    EspDnsResolver();

    Status resolve(const char* host_name) override;
    Status result(interfaces::network::Ipv4Address& address) override;
    void   cancel() override;

    /// Called from the lwIP thread with the answer to query id.
    void complete(std::uint32_t id, bool found, const interfaces::network::Ipv4Address& address);

private:
    enum class State : std::uint8_t
    {
        idle,
        pending,
        found,
        not_found,
    };
    /// Lookups whose answer may still arrive after cancel().
    static constexpr std::size_t kQuerySlots = 4;

    EspDnsQuery* free_query_();

    char                                 name_[kMaxHostNameLength + 1] = {};
    std::array<EspDnsQuery, kQuerySlots> queries_;
    std::uint32_t                        next_id_ = 0;
    std::atomic<std::uint32_t>           current_id_{ 0 };
    std::atomic<State>                   state_{ State::idle };
    std::atomic<std::uint32_t>           address_{ 0 };  ///< octets packed most significant first
};

}  // namespace brick::platform::esp32
