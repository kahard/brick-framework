#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/core/network/RemoteNetworkProtocol.h"
#include "brick/core/network/SerialPacketLink.h"
#include "brick/interfaces/network/IDnsResolver.h"
#include "brick/interfaces/network/ITcpListener.h"
#include "brick/interfaces/network/ITcpSocket.h"
#include "brick/interfaces/network/IUdpSocket.h"
#include "brick/interfaces/network/IWifiAccessPoint.h"
#include "brick/interfaces/network/IWifiStation.h"
#include "brick/interfaces/system/IFirmwareUpdate.h"

namespace brick::core::network
{

class RemoteNetworkHost;

namespace remote
{
    /// A byte ring in memory from the host's provider.
    class ByteRing final
    {
    public:
        bool         attach(std::uint8_t* memory, std::size_t capacity);
        void         clear() { head_ = tail_ = used_ = 0; }
        std::size_t  capacity() const { return capacity_; }
        std::size_t  used() const { return used_; }
        std::size_t  room() const { return capacity_ - used_; }
        bool         write(const std::uint8_t* data, std::size_t size);
        std::size_t  read(std::uint8_t* data, std::size_t capacity);
        std::uint8_t peek(std::size_t offset) const { return memory_[(tail_ + offset) % capacity_]; }
        void         skip(std::size_t size);

    private:
        std::uint8_t* memory_   = nullptr;
        std::size_t   capacity_ = 0;
        std::size_t   head_     = 0;
        std::size_t   tail_     = 0;
        std::size_t   used_     = 0;
    };

    class HostStation final : public brick::interfaces::network::IWifiStation
    {
    public:
        explicit HostStation(RemoteNetworkHost& host) : host_(host) {}

        const char*                           name() const override { return "wifi"; }
        brick::interfaces::network::LinkState link_state() const override { return link_; }
        Status                                ip_info(brick::interfaces::network::IpInfo& info) const override;
        Status                                mac_address(brick::interfaces::network::MacAddress& mac) const override;
        Status                                set_default_route() override;
        Status                                start_scan() override;
        Status scan_results(brick::interfaces::network::WifiNetwork* networks, std::size_t capacity,
                            std::size_t& count) override;
        Status connect(const brick::interfaces::network::WifiCredentials& credentials) override;
        Status disconnect() override;
        brick::interfaces::network::WifiFailure failure() const override { return failure_; }
        Status connected_network(brick::interfaces::network::WifiNetwork& network) const override;

    private:
        friend class brick::core::network::RemoteNetworkHost;

        RemoteNetworkHost&                      host_;
        std::uint8_t                            request_ = 0;
        brick::interfaces::network::LinkState   link_    = brick::interfaces::network::LinkState::down;
        brick::interfaces::network::WifiFailure failure_ = brick::interfaces::network::WifiFailure::none;
        brick::interfaces::network::IpInfo      ip_;
        brick::interfaces::network::MacAddress  mac_;
        bool                                    joined_ = false;
        brick::interfaces::network::WifiNetwork network_;
        Status                                  scan_       = Status::not_ready;
        std::size_t                             scan_count_ = 0;
        std::array<brick::interfaces::network::WifiNetwork, kMaxScanResults> scan_results_{};
    };

    class HostAccessPoint final : public brick::interfaces::network::IWifiAccessPoint
    {
    public:
        explicit HostAccessPoint(RemoteNetworkHost& host) : host_(host) {}

        const char*                           name() const override { return "hotspot"; }
        brick::interfaces::network::LinkState link_state() const override { return link_; }
        Status                                ip_info(brick::interfaces::network::IpInfo& info) const override;
        Status                                mac_address(brick::interfaces::network::MacAddress& mac) const override;
        Status                                set_default_route() override;
        Status      start(const brick::interfaces::network::WifiAccessPointConfig& config) override;
        Status      stop() override;
        std::size_t client_count() const override { return clients_; }

    private:
        friend class brick::core::network::RemoteNetworkHost;
        static constexpr std::size_t kTextBytes = 65;

        void queue_start_();

        RemoteNetworkHost&                     host_;
        std::uint8_t                           request_ = 0;
        brick::interfaces::network::LinkState  link_    = brick::interfaces::network::LinkState::down;
        brick::interfaces::network::IpInfo     ip_;
        brick::interfaces::network::MacAddress mac_;
        std::size_t                            clients_ = 0;
        bool                                   started_ = false;
        // What start() asked, sent again after the device restarted.
        char         ssid_[kTextBytes]{};
        char         password_[kTextBytes]{};
        std::uint8_t channel_     = 1;
        std::uint8_t max_clients_ = 4;
        bool         hidden_      = false;
    };

    class HostDnsResolver final : public brick::interfaces::network::IDnsResolver
    {
    public:
        explicit HostDnsResolver(RemoteNetworkHost& host) : host_(host) {}

        Status resolve(const char* host_name) override;
        Status result(brick::interfaces::network::Ipv4Address& address) override;
        void   cancel() override;

    private:
        friend class brick::core::network::RemoteNetworkHost;

        RemoteNetworkHost&                      host_;
        std::uint8_t                            lookup_ = 0;
        Status                                  state_  = Status::not_ready;
        brick::interfaces::network::Ipv4Address address_;
    };

    class HostTcpSocket final : public brick::interfaces::network::ITcpSocket
    {
    public:
        HostTcpSocket() = default;

        Status connect(const brick::interfaces::network::Endpoint& remote) override;
        Status poll_connect() override;
        Status send(const std::uint8_t* data, std::size_t size, std::size_t& sent) override;
        Status receive(std::uint8_t* data, std::size_t capacity, std::size_t& received) override;
        void   close() override;

    private:
        friend class brick::core::network::RemoteNetworkHost;
        friend class HostTcpListener;

        enum class State : std::uint8_t
        {
            closed,
            connecting,
            connected,
            peer_closed,  ///< the peer closed; what it sent can still be read
            failed,
        };

        void opened_(std::uint8_t generation);
        void credit_(bool all);

        RemoteNetworkHost* host_       = nullptr;
        std::uint8_t       index_      = 0;
        std::uint8_t       generation_ = 0;
        State              state_      = State::closed;
        Status             failure_    = Status::error;
        ByteRing           ring_;
        std::uint32_t      send_credit_ = 0;
        std::uint32_t      read_since_  = 0;      ///< bytes read, not yet given back as credit
        bool               waiting_     = false;  ///< accepted, not yet taken by accept()
    };

    class HostTcpListener final : public brick::interfaces::network::ITcpListener
    {
    public:
        explicit HostTcpListener(RemoteNetworkHost& host) : host_(host) {}

        Status                                  listen(std::uint16_t port) override;
        brick::interfaces::network::ITcpSocket* accept() override;
        void                                    close() override;

    private:
        friend class brick::core::network::RemoteNetworkHost;

        RemoteNetworkHost& host_;
        std::uint16_t      port_      = 0;
        bool               listening_ = false;
    };

    class HostUdpSocket final : public brick::interfaces::network::IUdpSocket
    {
    public:
        explicit HostUdpSocket(RemoteNetworkHost& host) : host_(host) {}

        Status open(std::uint16_t port) override;
        Status receive_from(std::uint8_t* data, std::size_t capacity, std::size_t& received,
                            brick::interfaces::network::Endpoint& from) override;
        Status send_to(const brick::interfaces::network::Endpoint& to, const std::uint8_t* data,
                       std::size_t size) override;
        void   close() override;

    private:
        friend class brick::core::network::RemoteNetworkHost;

        RemoteNetworkHost& host_;
        ByteRing           ring_;
        std::uint16_t      port_ = 0;
        bool               open_ = false;
    };

    /// The device's own program, replaced from the host: the device (with an
    /// IFirmwareUpdate in its configuration) writes the image as it arrives
    /// and starts it at restart(). Until then, and after an error, it runs
    /// what it ran. The link starts over when the device restarts: once
    /// device_ready() is true again, description() names the program that
    /// came up.
    class HostFirmwareUpdate final : public brick::interfaces::system::IFirmwareUpdate
    {
    public:
        explicit HostFirmwareUpdate(RemoteNetworkHost& host) : host_(host) {}

        /// not_ready: the device has not answered yet.
        Status begin(std::uint32_t size) override;
        /// Takes what the link and the device have room for now.
        Status write(const std::uint8_t* data, std::size_t size, std::size_t& written) override;
        Status finish() override;
        void   abort() override;
        Status restart() override;
        /// What the device said after the link last started; empty until
        /// then, and from a device that takes no update.
        const char* description() const override { return description_; }

        /// Bytes of the image the device has written.
        std::uint32_t written() const { return written_; }

    private:
        friend class brick::core::network::RemoteNetworkHost;

        enum class State : std::uint8_t
        {
            idle,
            starting,  ///< asked, the device has not answered
            writing,
            finishing,  ///< all written, the device checks it
            finished,
            failed,
        };

        bool running_() const
        {
            return state_ == State::starting || state_ == State::writing || state_ == State::finishing;
        }

        RemoteNetworkHost& host_;
        State              state_   = State::idle;
        Status             failure_ = Status::error;
        std::uint8_t       number_  = 0;
        std::uint32_t      size_    = 0;
        std::uint32_t      sent_    = 0;
        std::uint32_t      written_ = 0;
        char               description_[kDescriptionBytes]{};
    };
}  // namespace remote

struct RemoteNetworkHostConfig
{
    SerialPacketLinkConfig link;
    /// Receive ring of each of the host's own connections (the first one
    /// carries a video stream) and of each accepted one.
    std::array<std::size_t, remote::kClientSlots> client_receive_bytes{ { 32U * 1024U, 4096U } };
    std::size_t                                   accepted_receive_bytes = 4096;
    std::size_t                                   udp_receive_bytes      = 2048;
    /// No word from the device for this long: the link starts over.
    std::uint32_t device_timeout_ms = 5000;
};

/// The network of a Wi-Fi co-processor (RemoteNetworkDevice) behind a serial
/// port, as BRICK network interfaces: a station, a hotspot, DNS, TCP client
/// connections, a TCP listener and a UDP socket. Every call works on what
/// the device last reported and queues requests to it; nothing blocks. Each
/// interface call polls the link, and so should the application's loop.
/// After the device restarts, connections report errors and the station is
/// down; the listener, the UDP socket and the hotspot are opened again.
class RemoteNetworkHost final : private IPacketSink
{
public:
    explicit RemoteNetworkHost(RemoteNetworkHostConfig config);

    RemoteNetworkHost(const RemoteNetworkHost&)            = delete;
    RemoteNetworkHost& operator=(const RemoteNetworkHost&) = delete;

    Status begin();
    void   poll();

    /// The device has answered since the link last started.
    bool device_ready() const { return link_.connected() && device_ready_; }

    brick::interfaces::network::IWifiStation&     station() { return station_; }
    brick::interfaces::network::IWifiAccessPoint& access_point() { return access_point_; }
    brick::interfaces::network::IDnsResolver&     dns() { return dns_; }
    /// One of the host's own connections (index < remote::kClientSlots).
    brick::interfaces::network::ITcpSocket&   socket(std::size_t index) { return sockets_[index]; }
    brick::interfaces::network::ITcpListener& listener() { return listener_; }
    brick::interfaces::network::IUdpSocket&   udp() { return udp_; }
    /// The device's program: a new one for it, and the name of the one it runs.
    remote::HostFirmwareUpdate& update() { return update_; }
    const SerialPacketLink&     link() const { return link_; }
    /// Requests dropped because the queue was full, data over a ring.
    std::uint32_t overflows() const { return overflows_; }

private:
    friend class remote::HostStation;
    friend class remote::HostAccessPoint;
    friend class remote::HostDnsResolver;
    friend class remote::HostTcpSocket;
    friend class remote::HostTcpListener;
    friend class remote::HostUdpSocket;
    friend class remote::HostFirmwareUpdate;

    static constexpr std::size_t kQueueEntries = 16;
    static constexpr std::size_t kQueueBytes   = 160;

    struct Queued
    {
        std::uint8_t                          type = 0;
        std::uint8_t                          size = 0;
        std::array<std::uint8_t, kQueueBytes> payload{};
    };

    void on_packet(std::uint8_t type, const std::uint8_t* payload, std::size_t size) override;
    void on_link_reset() override;

    void on_state_(remote::PacketReader& reader);
    void on_scan_(remote::PacketReader& reader);
    void on_tcp_(remote::Message message, remote::PacketReader& reader);
    void on_udp_(remote::PacketReader& reader);
    void on_update_(remote::PacketReader& reader);

    /// Queues a request for the device; false when the queue is full.
    bool queue_(remote::Message message, const remote::PacketWriter& writer);
    void flush_();
    /// Data may go straight to the link: no request waits before it.
    bool data_room_() const;
    bool send_data_(remote::Message message, const remote::PacketWriter& head, const std::uint8_t* data,
                    std::size_t size);

    RemoteNetworkHostConfig                              config_;
    SerialPacketLink                                     link_;
    remote::HostStation                                  station_;
    remote::HostAccessPoint                              access_point_;
    remote::HostDnsResolver                              dns_;
    std::array<remote::HostTcpSocket, remote::kTcpSlots> sockets_{};
    remote::HostTcpListener                              listener_;
    remote::HostUdpSocket                                udp_;
    remote::HostFirmwareUpdate                           update_;
    std::array<Queued, kQueueEntries>                    queue_entries_{};
    std::size_t                                          queue_head_   = 0;
    std::size_t                                          queue_count_  = 0;
    bool                                                 device_ready_ = false;
    bool                                                 polling_      = false;
    std::uint32_t                                        overflows_    = 0;
};

}  // namespace brick::core::network
