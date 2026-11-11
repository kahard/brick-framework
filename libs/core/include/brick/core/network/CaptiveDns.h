#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/logging/ILogger.h"
#include "brick/interfaces/network/IUdpSocket.h"
#include "brick/interfaces/network/NetworkTypes.h"

namespace brick::core::network
{

/// Turns the DNS query in message (size bytes) into its answer in place:
/// an A query gets answer as the address, any other type an empty answer.
/// Returns the size of the answer, 0 for anything that is not a single
/// question (that is dropped). capacity is the room of message.
std::size_t captive_dns_answer(std::uint8_t* message, std::size_t size, std::size_t capacity,
                               const interfaces::network::Ipv4Address& answer);

/// DNS server of a setup hotspot: every name points to the device, so a
/// phone that joins the hotspot finds the setup page by itself (its
/// connectivity check lands there). Call update() from the loop.
class CaptiveDns final
{
public:
    static constexpr std::uint16_t kPort = 53;

    CaptiveDns(interfaces::network::IUdpSocket& socket, interfaces::logging::ILogger& logger);

    /// Opens port 53 and answers every name with address.
    Status        start(const interfaces::network::Ipv4Address& address);
    void          stop();
    void          update();
    bool          running() const { return running_; }
    std::uint32_t answered() const { return answered_; }

private:
    interfaces::network::IUdpSocket& socket_;
    interfaces::logging::ILogger&    logger_;
    interfaces::network::Ipv4Address address_{};
    std::array<std::uint8_t, 512>    message_{};  ///< a DNS message over UDP is at most 512 bytes
    std::uint32_t                    answered_ = 0;
    bool                             running_  = false;
};

}  // namespace brick::core::network
