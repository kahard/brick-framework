#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/core/network/RemoteNetworkProtocol.h"
#include "brick/core/network/SerialPacketLink.h"
#include "brick/interfaces/logging/ILogger.h"
#include "brick/interfaces/network/IDnsResolver.h"
#include "brick/interfaces/network/ITcpListener.h"
#include "brick/interfaces/network/ITcpSocket.h"
#include "brick/interfaces/network/IUdpSocket.h"
#include "brick/interfaces/network/IWifiAccessPoint.h"
#include "brick/interfaces/network/IWifiStation.h"
#include "brick/interfaces/system/IFirmwareUpdate.h"

namespace brick::core::network
{

struct RemoteNetworkDeviceConfig
{
    /// The window should hold what the line carries while the host is busy
    /// (e.g. 32 packets: 32 ms at 10 Mbaud).
    SerialPacketLinkConfig                        link;
    brick::interfaces::network::IWifiStation*     station      = nullptr;
    brick::interfaces::network::IWifiAccessPoint* access_point = nullptr;
    brick::interfaces::network::IDnsResolver*     dns          = nullptr;
    /// The host's own TCP connections.
    std::array<brick::interfaces::network::ITcpSocket*, remote::kClientSlots> clients{};
    brick::interfaces::network::ITcpListener*                                 listener = nullptr;
    brick::interfaces::network::IUdpSocket*                                   udp      = nullptr;
    /// Optional: takes a new program for the co-processor from the host (see
    /// RemoteNetworkHost::update()) and restarts into it. Without it the host
    /// is told the co-processor takes none.
    brick::interfaces::system::IFirmwareUpdate* update = nullptr;
    /// Optional: requests that fail, link restarts.
    brick::interfaces::logging::ILogger* logger = nullptr;
};

/// The Wi-Fi co-processor's side of a remote network: carries out what
/// RemoteNetworkHost asks over the serial link with the co-processor's own
/// network interfaces, and reports their state, the data that arrives and
/// the connections that come in. Call poll() as often as possible.
class RemoteNetworkDevice final : private IPacketSink
{
public:
    explicit RemoteNetworkDevice(RemoteNetworkDeviceConfig config);

    Status begin();
    void   poll();

    bool                    host_connected() const { return link_.connected(); }
    const SerialPacketLink& link() const { return link_; }

private:
    enum class SlotState : std::uint8_t
    {
        idle,
        connecting,
        connected,
        closed,   ///< reported to the host, waiting for its close
        closing,  ///< closed by the host: what it sent goes out first
    };

    struct Slot
    {
        brick::interfaces::network::ITcpSocket* socket       = nullptr;
        std::uint8_t                            generation   = 0;
        SlotState                               state        = SlotState::idle;
        std::uint32_t                           credit       = 0;  ///< bytes the host has room for
        std::uint32_t                           closing_from = 0;
        std::uint8_t*                           pending      = nullptr;
        std::size_t                             pending_size = 0;
        std::uint32_t                           passed       = 0;  ///< sent on, not yet reported
        bool                                    status_due   = false;
        Status                                  status       = Status::ok;
        bool                                    closed_due   = false;
        bool                                    accepted_due = false;
    };

    void on_packet(std::uint8_t type, const std::uint8_t* payload, std::size_t size) override;
    void on_link_reset() override;

    void  handle_station_(remote::Message message, remote::PacketReader& reader);
    void  handle_access_point_(remote::Message message, remote::PacketReader& reader);
    void  handle_tcp_(remote::Message message, remote::PacketReader& reader);
    void  handle_other_(remote::Message message, remote::PacketReader& reader);
    Slot* slot_(remote::PacketReader& reader, bool client_only, std::size_t& index);
    void  close_slot_(std::size_t index);

    void service_tcp_();
    void service_slot_(std::size_t index);
    void flush_closing_(std::size_t index);
    void service_listener_();
    void service_udp_();
    void service_dns_();
    void service_scan_();
    void service_state_();
    void service_update_();
    void handle_update_(remote::Message message, remote::PacketReader& reader);
    void end_update_(Status status);
    bool send_(remote::Message message, const remote::PacketWriter& head, const std::uint8_t* data = nullptr,
               std::size_t size = 0);
    bool data_room_() const;
    void log_(const char* format, unsigned value) const;
    void note_(const char* format, unsigned value) const;

    RemoteNetworkDeviceConfig                                                    config_;
    SerialPacketLink                                                             link_;
    std::array<Slot, remote::kTcpSlots>                                          slots_{};
    std::uint32_t                                                                accepted_window_ = 0;
    bool                                                                         listening_       = false;
    bool                                                                         udp_open_        = false;
    bool                                                                         scanning_        = false;
    bool                                                                         scan_due_        = false;
    Status                                                                       scan_status_     = Status::ok;
    std::size_t                                                                  scan_count_      = 0;
    std::array<brick::interfaces::network::WifiNetwork, remote::kMaxScanResults> scan_results_{};
    bool                                                                         dns_pending_ = false;
    bool                                                                         dns_due_     = false;
    std::uint8_t                                                                 dns_lookup_  = 0;
    Status                                                                       dns_status_  = Status::ok;
    brick::interfaces::network::Ipv4Address                                      dns_address_;
    std::uint8_t                                                                 station_request_  = 0;
    std::uint8_t                                                                 ap_request_       = 0;
    bool                                                                         state_due_        = true;
    std::uint32_t                                                                state_sent_at_    = 0;
    std::uint32_t                                                                state_checked_at_ = 0;
    std::array<std::uint8_t, SerialPacketLink::kMaxPayload>                      last_state_{};
    std::size_t                                                                  last_state_size_ = 0;
    // The update of this device's program the host runs.
    remote::UpdatePhase update_phase_    = remote::UpdatePhase::idle;
    Status              update_status_   = Status::ok;
    std::uint8_t        update_number_   = 0;
    std::uint32_t       update_size_     = 0;
    std::uint32_t       update_written_  = 0;
    bool                update_due_      = false;  ///< its state goes to the host
    bool                description_due_ = false;
    bool                restart_due_     = false;
    std::uint32_t       restart_from_    = 0;
    /// A connection is read this many packets at a time: one call of the
    /// socket for all of them.
    static constexpr std::size_t kReadPackets = 4;

    std::array<std::uint8_t, kReadPackets * remote::kMaxDataBytes> scratch_{};
    std::array<std::uint8_t, SerialPacketLink::kMaxPayload>        message_{};
};

}  // namespace brick::core::network
