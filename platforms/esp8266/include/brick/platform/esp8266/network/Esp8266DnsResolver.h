#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/network/IDnsResolver.h"

namespace brick::platform::esp8266
{

class Esp8266DnsResolver;

/// One lookup handed to lwIP; its answer arrives later from the network stack.
struct Esp8266DnsQuery
{
    Esp8266DnsResolver* owner   = nullptr;
    std::uint32_t       id      = 0;
    bool                pending = false;
};

/// IDnsResolver over the lwIP DNS client, without blocking. The resolver must
/// outlive its last lookup (keep it static). lwIP reports "no such name" and
/// "no answer" alike, as Status::not_found.
class Esp8266DnsResolver final : public brick::interfaces::network::IDnsResolver
{
public:
    static constexpr std::size_t kMaxHostNameLength = 253;

    Esp8266DnsResolver();

    Status resolve(const char* host_name) override;
    Status result(brick::interfaces::network::Ipv4Address& address) override;
    void   cancel() override;

    /// Called by the network stack with the answer to query id.
    void complete(std::uint32_t id, bool found, const brick::interfaces::network::Ipv4Address& address);

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

    Esp8266DnsQuery* free_query_();

    char                                     name_[kMaxHostNameLength + 1] = {};
    std::array<Esp8266DnsQuery, kQuerySlots> queries_{};
    std::uint32_t                            next_id_    = 0;
    std::uint32_t                            current_id_ = 0;
    State                                    state_      = State::idle;
    brick::interfaces::network::Ipv4Address  address_;
};

}  // namespace brick::platform::esp8266
