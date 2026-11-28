#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "brick/Status.h"
#include "brick/core/network/SerialPacketLink.h"
#include "brick/interfaces/network/NetworkTypes.h"
#include "brick/interfaces/network/WifiTypes.h"

// The messages between RemoteNetworkHost (the application's processor) and
// RemoteNetworkDevice (the Wi-Fi co-processor) over a SerialPacketLink.
// Numbers are little-endian; a text is a length byte and its characters.
namespace brick::core::network::remote
{

inline constexpr std::uint8_t kProtocolVersion = 1;

/// TCP connections: the host's own (ITcpSocket::connect()) first, then the
/// ones the listener accepted.
inline constexpr std::size_t kClientSlots   = 2;
inline constexpr std::size_t kAcceptedSlots = 4;
inline constexpr std::size_t kTcpSlots      = kClientSlots + kAcceptedSlots;
/// What the device holds for each connection of what the host sent: the
/// host's send credit.
inline constexpr std::size_t kDeviceSendBytes = 4096;
/// Networks a scan reports.
inline constexpr std::size_t kMaxScanResults = 16;
/// Data in one packet after its message header.
inline constexpr std::size_t kDataHeader   = 8;
inline constexpr std::size_t kMaxDataBytes = SerialPacketLink::kMaxPayload - kDataHeader;

enum class Message : std::uint8_t
{
    // Host to device.
    hello = SerialPacketLink::kFirstType,  ///< version; the device answers with its state
    wifi_scan,                             ///< request
    wifi_connect,                          ///< request, hidden, ssid, password
    wifi_disconnect,                       ///< request
    default_route,                         ///< interface (0 station, 1 access point)
    ap_start,                              ///< request, channel, max clients, hidden, ssid, password
    ap_stop,                               ///< request
    dns_resolve,                           ///< lookup, name
    dns_cancel,                            ///< lookup
    tcp_connect,                           ///< slot, generation, address, port, receive window (u32)
    tcp_send,                              ///< slot, generation, data
    tcp_close,                             ///< slot, generation
    tcp_credit,                            ///< slot, generation, bytes read (u32)
    listen,                                ///< port, receive window of an accepted connection (u32)
    listen_close,
    udp_open,  ///< port
    udp_send,  ///< address, port, data
    udp_close,
    update_begin,   ///< update, size of the image (u32)
    update_data,    ///< update, offset (u32), data
    update_finish,  ///< update
    update_abort,   ///< update
    restart,        ///< the device restarts (after an update: into the new program)

    // Device to host.
    state = 0x80,  ///< see RemoteState
    scan_done,     ///< status, count, networks
    dns_result,    ///< lookup, status, address
    tcp_status,    ///< slot, generation, status of poll_connect()
    tcp_data,      ///< slot, generation, data
    tcp_sent,      ///< slot, generation, bytes passed on (u32): send credit back
    tcp_closed,    ///< slot, generation, status of receive() (not_ready: closed by the peer)
    tcp_accepted,  ///< slot, generation
    udp_data,      ///< address, port, data
    update_state,  ///< update, phase (UpdatePhase), status, bytes written (u32)
    description,   ///< text naming the device's program; after each hello
};

/// Interfaces of default_route.
enum class Interface : std::uint8_t
{
    station,
    access_point,
};

/// Where an update of the device's program is, in update_state. The update
/// number is the host's: a state with another number is of an update before.
enum class UpdatePhase : std::uint8_t
{
    idle,
    writing,   ///< begun: the device takes the image
    checking,  ///< all of it written; the device checks it
    finished,  ///< checked: it starts at the device's next restart
    failed,    ///< ended, with the status that says why
};

/// Bytes of an image in one update_data, and how far the host may be ahead of
/// what the device has reported written: the device is busy with its flash
/// for tens of milliseconds at a time, and its serial driver holds only a
/// few packets meanwhile.
inline constexpr std::size_t   kMaxUpdateData    = kMaxDataBytes;
inline constexpr std::uint32_t kUpdateAheadBytes = 2U * kMaxDataBytes;
/// The text naming the device's program, with its end.
inline constexpr std::size_t kDescriptionBytes = 96;

/// Builds a message payload; a write past the end marks it failed.
class PacketWriter final
{
public:
    PacketWriter(std::uint8_t* data, std::size_t capacity) : data_(data), capacity_(capacity) {}

    void u8(std::uint8_t value) { bytes(&value, 1); }
    void u16(std::uint16_t value)
    {
        const std::uint8_t bytes_le[] = { static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8U) };
        bytes(bytes_le, sizeof(bytes_le));
    }
    void u32(std::uint32_t value)
    {
        u16(static_cast<std::uint16_t>(value));
        u16(static_cast<std::uint16_t>(value >> 16U));
    }
    void status(Status value) { u8(static_cast<std::uint8_t>(value)); }
    void bytes(const void* source, std::size_t count)
    {
        if (count > capacity_ - size_)
        {
            failed_ = true;
            return;
        }
        if (count != 0)
            std::memcpy(data_ + size_, source, count);
        size_ += count;
    }
    /// Up to 255 characters; a null pointer is an empty text.
    void text(const char* value)
    {
        const std::size_t length = value != nullptr ? std::strlen(value) : 0U;
        if (length > kMaxText)
        {
            failed_ = true;
            return;
        }
        u8(static_cast<std::uint8_t>(length));
        bytes(value, length);
    }
    void address(const brick::interfaces::network::Ipv4Address& value)
    {
        bytes(value.octets.data(), value.octets.size());
    }
    void ip_info(const brick::interfaces::network::IpInfo& value)
    {
        address(value.address);
        address(value.netmask);
        address(value.gateway);
        address(value.dns);
    }
    void mac(const brick::interfaces::network::MacAddress& value) { bytes(value.octets.data(), value.octets.size()); }
    void network(const brick::interfaces::network::WifiNetwork& value)
    {
        text(value.ssid);
        mac(value.bssid);
        u8(static_cast<std::uint8_t>(value.rssi_dbm));
        u8(value.channel);
        u8(static_cast<std::uint8_t>(value.security));
    }

    const std::uint8_t* data() const { return data_; }
    std::size_t         size() const { return size_; }
    bool                ok() const { return !failed_; }

private:
    static constexpr std::size_t kMaxText = 255;

    std::uint8_t* data_;
    std::size_t   capacity_;
    std::size_t   size_   = 0;
    bool          failed_ = false;
};

/// Reads a message payload; a read past the end marks it failed and gives zeros.
class PacketReader final
{
public:
    PacketReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::uint8_t u8()
    {
        std::uint8_t value = 0;
        bytes(&value, 1);
        return value;
    }
    std::uint16_t u16()
    {
        const std::uint8_t low = u8();
        return static_cast<std::uint16_t>(low | (u8() << 8U));
    }
    std::uint32_t u32()
    {
        const std::uint16_t low = u16();
        return static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(u16()) << 16U);
    }
    Status status() { return static_cast<Status>(u8()); }
    void   bytes(void* target, std::size_t count)
    {
        if (count > size_ - position_)
        {
            failed_   = true;
            position_ = size_;
            std::memset(target, 0, count);
            return;
        }
        std::memcpy(target, data_ + position_, count);
        position_ += count;
    }
    /// Copies a text into capacity bytes (cut short to fit), always terminated.
    void text(char* target, std::size_t capacity)
    {
        const std::size_t length = u8();
        if (length > size_ - position_ || capacity == 0)
        {
            failed_   = true;
            position_ = size_;
            if (capacity != 0)
                target[0] = '\0';
            return;
        }
        const std::size_t kept = length < capacity - 1U ? length : capacity - 1U;
        std::memcpy(target, data_ + position_, kept);
        target[kept] = '\0';
        position_ += length;
    }
    brick::interfaces::network::Ipv4Address address()
    {
        brick::interfaces::network::Ipv4Address value;
        bytes(value.octets.data(), value.octets.size());
        return value;
    }
    brick::interfaces::network::IpInfo ip_info()
    {
        brick::interfaces::network::IpInfo value;
        value.address = address();
        value.netmask = address();
        value.gateway = address();
        value.dns     = address();
        return value;
    }
    brick::interfaces::network::MacAddress mac()
    {
        brick::interfaces::network::MacAddress value;
        bytes(value.octets.data(), value.octets.size());
        return value;
    }
    brick::interfaces::network::WifiNetwork network()
    {
        brick::interfaces::network::WifiNetwork value;
        text(value.ssid, sizeof(value.ssid));
        value.bssid    = mac();
        value.rssi_dbm = static_cast<std::int8_t>(u8());
        value.channel  = u8();
        value.security = static_cast<brick::interfaces::network::WifiSecurity>(u8());
        return value;
    }

    /// What is left (the data of a data message).
    const std::uint8_t* rest() const { return data_ + position_; }
    std::size_t         remaining() const { return size_ - position_; }
    bool                ok() const { return !failed_; }

private:
    const std::uint8_t* data_;
    std::size_t         size_;
    std::size_t         position_ = 0;
    bool                failed_   = false;
};

/// What the device reports of its interfaces, in a state message: on a
/// change and every kStateIntervalMs. The request numbers say which of the
/// host's station and access point requests the device had done by then.
struct RemoteState
{
    std::uint8_t                            station_request = 0;
    brick::interfaces::network::LinkState   station_link    = brick::interfaces::network::LinkState::down;
    brick::interfaces::network::WifiFailure station_failure = brick::interfaces::network::WifiFailure::none;
    brick::interfaces::network::IpInfo      station_ip;
    brick::interfaces::network::MacAddress  station_mac;
    bool                                    joined = false;
    brick::interfaces::network::WifiNetwork network;
    std::uint8_t                            ap_request = 0;
    brick::interfaces::network::LinkState   ap_link    = brick::interfaces::network::LinkState::down;
    brick::interfaces::network::IpInfo      ap_ip;
    brick::interfaces::network::MacAddress  ap_mac;
    std::uint8_t                            ap_clients = 0;

    void write(PacketWriter& writer) const
    {
        writer.u8(station_request);
        writer.u8(static_cast<std::uint8_t>(station_link));
        writer.u8(static_cast<std::uint8_t>(station_failure));
        writer.ip_info(station_ip);
        writer.mac(station_mac);
        writer.u8(joined ? 1U : 0U);
        writer.network(network);
        writer.u8(ap_request);
        writer.u8(static_cast<std::uint8_t>(ap_link));
        writer.ip_info(ap_ip);
        writer.mac(ap_mac);
        writer.u8(ap_clients);
    }

    void read(PacketReader& reader)
    {
        station_request = reader.u8();
        station_link    = static_cast<brick::interfaces::network::LinkState>(reader.u8());
        station_failure = static_cast<brick::interfaces::network::WifiFailure>(reader.u8());
        station_ip      = reader.ip_info();
        station_mac     = reader.mac();
        joined          = reader.u8() != 0;
        network         = reader.network();
        ap_request      = reader.u8();
        ap_link         = static_cast<brick::interfaces::network::LinkState>(reader.u8());
        ap_ip           = reader.ip_info();
        ap_mac          = reader.mac();
        ap_clients      = reader.u8();
    }
};

inline constexpr std::uint32_t kStateIntervalMs = 500;

}  // namespace brick::core::network::remote
