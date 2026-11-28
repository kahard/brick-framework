#include "brick/core/network/RemoteNetworkHost.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>

#include "brick/core/network/Ipv4.h"
#include "brick/core/network/WifiConfigCheck.h"

namespace brick::core::network
{

namespace
{
    using brick::interfaces::network::LinkState;
    using brick::interfaces::network::WifiFailure;
    using remote::Message;
    using remote::PacketReader;
    using remote::PacketWriter;

    constexpr std::size_t kHeadBytes       = 16;
    constexpr std::size_t kControlReserve  = 2;  // packets data leaves free for requests
    constexpr std::size_t kUdpRecordHeader = 8;  // length (u16), address, port (u16)
    constexpr std::size_t kMaxHostName     = 253;
    constexpr std::size_t kCreditFraction  = 4;  // credit goes back after a quarter of the ring is read
    constexpr std::size_t kCredentialBytes = kHeadBytes + 2U * 65U;  // SSID and password as texts
}  // namespace

namespace remote
{

    // ---------------------------------------------------------------- ByteRing

    bool ByteRing::attach(std::uint8_t* memory, std::size_t capacity)
    {
        memory_   = memory;
        capacity_ = memory != nullptr ? capacity : 0U;
        clear();
        return capacity_ != 0;
    }

    bool ByteRing::write(const std::uint8_t* data, std::size_t size)
    {
        if (size > room())
            return false;
        if (size == 0)
            return true;
        const std::size_t first = std::min(size, capacity_ - head_);
        std::memcpy(memory_ + head_, data, first);
        if (size > first)
            std::memcpy(memory_, data + first, size - first);
        head_ = (head_ + size) % capacity_;
        used_ += size;
        return true;
    }

    std::size_t ByteRing::read(std::uint8_t* data, std::size_t capacity)
    {
        const std::size_t count = std::min(capacity, used_);
        if (count == 0)
            return 0;
        const std::size_t first = std::min(count, capacity_ - tail_);
        std::memcpy(data, memory_ + tail_, first);
        if (count > first)
            std::memcpy(data + first, memory_, count - first);
        tail_ = (tail_ + count) % capacity_;
        used_ -= count;
        return count;
    }

    void ByteRing::skip(std::size_t size)
    {
        size  = std::min(size, used_);
        tail_ = capacity_ != 0 ? (tail_ + size) % capacity_ : 0U;
        used_ -= size;
    }

    // ------------------------------------------------------------- HostStation

    Status HostStation::ip_info(brick::interfaces::network::IpInfo& info) const
    {
        if (link_ != LinkState::up)
            return Status::not_ready;
        info = ip_;
        return Status::ok;
    }

    Status HostStation::mac_address(brick::interfaces::network::MacAddress& mac) const
    {
        if (!host_.device_ready())
            return Status::not_ready;
        mac = mac_;
        return Status::ok;
    }

    Status HostStation::set_default_route()
    {
        if (link_ != LinkState::up)
            return Status::not_ready;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(static_cast<std::uint8_t>(Interface::station));
        return host_.queue_(Message::default_route, writer) ? Status::ok : Status::busy;
    }

    Status HostStation::start_scan()
    {
        host_.poll();
        if (scan_ == Status::busy)
            return Status::busy;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(0);
        if (!host_.queue_(Message::wifi_scan, writer))
            return Status::error;
        scan_       = Status::busy;
        scan_count_ = 0;
        return Status::ok;
    }

    Status HostStation::scan_results(brick::interfaces::network::WifiNetwork* networks, std::size_t capacity,
                                     std::size_t& count)
    {
        host_.poll();
        count = 0;
        if (scan_ != Status::ok)
            return scan_;
        count = std::min(capacity, scan_count_);
        for (std::size_t index = 0; index < count; ++index)
            networks[index] = scan_results_[index];
        return Status::ok;
    }

    Status HostStation::connect(const brick::interfaces::network::WifiCredentials& credentials)
    {
        if (const Status status = check_credentials(credentials); !is_ok(status))
            return status;
        request_ = static_cast<std::uint8_t>(request_ + 1U);
        link_    = LinkState::connecting;
        failure_ = WifiFailure::none;
        joined_  = false;
        std::uint8_t bytes[kCredentialBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(request_);
        writer.u8(credentials.hidden ? 1U : 0U);
        writer.text(credentials.ssid);
        writer.text(credentials.password);
        return host_.queue_(Message::wifi_connect, writer) ? Status::ok : Status::error;
    }

    Status HostStation::disconnect()
    {
        request_ = static_cast<std::uint8_t>(request_ + 1U);
        link_    = LinkState::down;
        failure_ = WifiFailure::none;
        joined_  = false;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(request_);
        return host_.queue_(Message::wifi_disconnect, writer) ? Status::ok : Status::error;
    }

    Status HostStation::connected_network(brick::interfaces::network::WifiNetwork& network) const
    {
        if (link_ != LinkState::up || !joined_)
            return Status::not_ready;
        network = network_;
        return Status::ok;
    }

    // --------------------------------------------------------- HostAccessPoint

    Status HostAccessPoint::ip_info(brick::interfaces::network::IpInfo& info) const
    {
        if (link_ != LinkState::up)
            return Status::not_ready;
        info = ip_;
        return Status::ok;
    }

    Status HostAccessPoint::mac_address(brick::interfaces::network::MacAddress& mac) const
    {
        if (!host_.device_ready())
            return Status::not_ready;
        mac = mac_;
        return Status::ok;
    }

    Status HostAccessPoint::set_default_route()
    {
        if (link_ != LinkState::up)
            return Status::not_ready;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(static_cast<std::uint8_t>(Interface::access_point));
        return host_.queue_(Message::default_route, writer) ? Status::ok : Status::busy;
    }

    Status HostAccessPoint::start(const brick::interfaces::network::WifiAccessPointConfig& config)
    {
        if (const Status status = check_access_point(config); !is_ok(status))
            return status;
        std::strncpy(ssid_, config.ssid, sizeof(ssid_) - 1U);
        std::strncpy(password_, config.password != nullptr ? config.password : "", sizeof(password_) - 1U);
        channel_     = config.channel;
        max_clients_ = config.max_clients;
        hidden_      = config.hidden;
        started_     = true;
        link_        = LinkState::connecting;
        queue_start_();
        return Status::ok;
    }

    void HostAccessPoint::queue_start_()
    {
        request_ = static_cast<std::uint8_t>(request_ + 1U);
        std::uint8_t bytes[kHeadBytes + 2U * kTextBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(request_);
        writer.u8(channel_);
        writer.u8(max_clients_);
        writer.u8(hidden_ ? 1U : 0U);
        writer.text(ssid_);
        writer.text(password_);
        (void)host_.queue_(Message::ap_start, writer);
    }

    Status HostAccessPoint::stop()
    {
        started_ = false;
        request_ = static_cast<std::uint8_t>(request_ + 1U);
        link_    = LinkState::down;
        clients_ = 0;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(request_);
        return host_.queue_(Message::ap_stop, writer) ? Status::ok : Status::error;
    }

    // --------------------------------------------------------- HostDnsResolver

    Status HostDnsResolver::resolve(const char* host_name)
    {
        if (host_name == nullptr || host_name[0] == '\0' || std::strlen(host_name) > kMaxHostName)
            return Status::invalid_argument;
        if (parse_ipv4(host_name, address_))
        {
            state_ = Status::ok;
            return Status::ok;
        }
        lookup_ = static_cast<std::uint8_t>(lookup_ + 1U);
        std::uint8_t bytes[kMaxHostName + kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(lookup_);
        writer.text(host_name);
        state_ = host_.queue_(Message::dns_resolve, writer) ? Status::busy : Status::timeout;
        return Status::ok;
    }

    Status HostDnsResolver::result(brick::interfaces::network::Ipv4Address& address)
    {
        host_.poll();
        if (state_ == Status::ok)
            address = address_;
        return state_;
    }

    void HostDnsResolver::cancel()
    {
        if (state_ == Status::busy)
        {
            std::uint8_t bytes[kHeadBytes];
            PacketWriter writer(bytes, sizeof(bytes));
            writer.u8(lookup_);
            (void)host_.queue_(Message::dns_cancel, writer);
        }
        state_ = Status::not_ready;
    }

    // ----------------------------------------------------------- HostTcpSocket

    Status HostTcpSocket::connect(const brick::interfaces::network::Endpoint& remote_end)
    {
        if (index_ >= kClientSlots)
            return Status::not_supported;
        host_->poll();
        if (!host_->device_ready())
            return Status::not_ready;
        close();
        opened_(static_cast<std::uint8_t>(generation_ + 1U));
        state_ = State::connecting;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(index_);
        writer.u8(generation_);
        writer.address(remote_end.address);
        writer.u16(remote_end.port);
        writer.u32(static_cast<std::uint32_t>(ring_.capacity()));
        if (!host_->queue_(Message::tcp_connect, writer))
            state_ = State::failed;
        return Status::ok;
    }

    Status HostTcpSocket::poll_connect()
    {
        host_->poll();
        switch (state_)
        {
            case State::connecting:
                return Status::busy;
            case State::connected:
            case State::peer_closed:
                return Status::ok;
            case State::failed:
                return failure_;
            case State::closed:
                break;
        }
        return Status::not_ready;
    }

    Status HostTcpSocket::send(const std::uint8_t* data, std::size_t size, std::size_t& sent)
    {
        host_->poll();
        sent = 0;
        if (state_ == State::failed || state_ == State::peer_closed)
            return Status::error;
        if (state_ != State::connected)
            return Status::not_ready;
        while (sent < size && send_credit_ != 0)
        {
            const std::size_t count = std::min({ size - sent, static_cast<std::size_t>(send_credit_), kMaxDataBytes });
            std::uint8_t      bytes[kHeadBytes];
            PacketWriter      head(bytes, sizeof(bytes));
            head.u8(index_);
            head.u8(generation_);
            if (!host_->send_data_(Message::tcp_send, head, data + sent, count))
                break;
            sent += count;
            send_credit_ -= static_cast<std::uint32_t>(count);
        }
        if (sent == 0)
            return Status::busy;
        host_->poll();
        return Status::ok;
    }

    Status HostTcpSocket::receive(std::uint8_t* data, std::size_t capacity, std::size_t& received)
    {
        host_->poll();
        received = ring_.read(data, capacity);
        read_since_ += static_cast<std::uint32_t>(received);
        credit_(false);
        if (received != 0)
            return Status::ok;
        switch (state_)
        {
            case State::connecting:
            case State::connected:
                return Status::no_data;
            case State::failed:
                return failure_;
            case State::peer_closed:
            case State::closed:
                break;
        }
        return Status::not_ready;
    }

    void HostTcpSocket::close()
    {
        if (state_ == State::closed)
            return;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(index_);
        writer.u8(generation_);
        (void)host_->queue_(Message::tcp_close, writer);
        state_      = State::closed;
        waiting_    = false;
        read_since_ = 0;
        ring_.clear();
    }

    void HostTcpSocket::opened_(std::uint8_t generation)
    {
        generation_  = generation;
        state_       = State::connected;
        failure_     = Status::error;
        send_credit_ = static_cast<std::uint32_t>(kDeviceSendBytes);
        read_since_  = 0;
        waiting_     = false;
        ring_.clear();
    }

    void HostTcpSocket::credit_(bool all)
    {
        if (state_ != State::connected || read_since_ == 0)
            return;
        if (!all && read_since_ < ring_.capacity() / kCreditFraction && ring_.used() != 0)
            return;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(index_);
        writer.u8(generation_);
        writer.u32(read_since_);
        if (host_->queue_(Message::tcp_credit, writer))
            read_since_ = 0;
    }

    // --------------------------------------------------------- HostTcpListener

    Status HostTcpListener::listen(std::uint16_t port)
    {
        host_.poll();
        if (!host_.device_ready())
            return Status::not_ready;
        port_      = port;
        listening_ = true;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u16(port_);
        writer.u32(static_cast<std::uint32_t>(host_.config_.accepted_receive_bytes));
        return host_.queue_(Message::listen, writer) ? Status::ok : Status::busy;
    }

    brick::interfaces::network::ITcpSocket* HostTcpListener::accept()
    {
        host_.poll();
        if (!listening_)
            return nullptr;
        for (std::size_t index = kClientSlots; index < kTcpSlots; ++index)
        {
            HostTcpSocket& socket = host_.sockets_[index];
            if (socket.waiting_)
            {
                socket.waiting_ = false;
                return &socket;
            }
        }
        return nullptr;
    }

    void HostTcpListener::close()
    {
        if (listening_)
        {
            std::uint8_t bytes[kHeadBytes];
            PacketWriter writer(bytes, sizeof(bytes));
            (void)host_.queue_(Message::listen_close, writer);
        }
        listening_ = false;
        // The device closes the accepted connections with the listener.
        for (std::size_t index = kClientSlots; index < kTcpSlots; ++index)
        {
            HostTcpSocket& socket = host_.sockets_[index];
            socket.state_         = HostTcpSocket::State::closed;
            socket.waiting_       = false;
            socket.ring_.clear();
        }
    }

    // ----------------------------------------------------------- HostUdpSocket

    Status HostUdpSocket::open(std::uint16_t port)
    {
        host_.poll();
        if (!host_.device_ready())
            return Status::not_ready;
        ring_.clear();
        port_ = port;
        open_ = true;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u16(port_);
        return host_.queue_(Message::udp_open, writer) ? Status::ok : Status::busy;
    }

    Status HostUdpSocket::receive_from(std::uint8_t* data, std::size_t capacity, std::size_t& received,
                                       brick::interfaces::network::Endpoint& from)
    {
        host_.poll();
        received = 0;
        if (!open_)
            return Status::not_ready;
        if (ring_.used() < kUdpRecordHeader)
            return Status::no_data;
        std::uint8_t header[kUdpRecordHeader];
        (void)ring_.read(header, sizeof(header));
        PacketReader      reader(header, sizeof(header));
        const std::size_t length = reader.u16();
        from.address             = reader.address();
        from.port                = reader.u16();
        received                 = ring_.read(data, std::min(length, capacity));
        ring_.skip(length - received);
        return Status::ok;
    }

    Status HostUdpSocket::send_to(const brick::interfaces::network::Endpoint& to, const std::uint8_t* data,
                                  std::size_t size)
    {
        host_.poll();
        if (!open_)
            return Status::not_ready;
        if (size > kMaxDataBytes)
            return Status::invalid_argument;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter head(bytes, sizeof(bytes));
        head.address(to.address);
        head.u16(to.port);
        if (!host_.send_data_(Message::udp_send, head, data, size))
            return Status::busy;
        host_.poll();
        return Status::ok;
    }

    void HostUdpSocket::close()
    {
        if (open_)
        {
            std::uint8_t bytes[kHeadBytes];
            PacketWriter writer(bytes, sizeof(bytes));
            (void)host_.queue_(Message::udp_close, writer);
        }
        open_ = false;
        ring_.clear();
    }

    // ------------------------------------------------------ HostFirmwareUpdate

    Status HostFirmwareUpdate::begin(std::uint32_t size)
    {
        host_.poll();
        if (!host_.device_ready())
            return Status::not_ready;
        if (running_())
            return Status::busy;
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(static_cast<std::uint8_t>(number_ + 1U));
        writer.u32(size);
        if (!host_.queue_(Message::update_begin, writer))
            return Status::busy;
        ++number_;
        state_   = State::starting;
        size_    = size;
        sent_    = 0;
        written_ = 0;
        return Status::ok;
    }

    Status HostFirmwareUpdate::write(const std::uint8_t* data, std::size_t size, std::size_t& written)
    {
        written = 0;
        host_.poll();
        if (state_ == State::failed)
            return failure_;
        if (state_ == State::starting)
            return Status::ok;  // the device has not said yet that it takes the image
        if (state_ != State::writing)
            return Status::not_ready;
        if (size > size_ - sent_)
            return Status::invalid_argument;
        while (written < size && sent_ - written_ < kUpdateAheadBytes)
        {
            const std::size_t chunk =
                std::min({ size - written, kMaxUpdateData, std::size_t{ kUpdateAheadBytes - (sent_ - written_) } });
            std::uint8_t bytes[kHeadBytes];
            PacketWriter head(bytes, sizeof(bytes));
            head.u8(number_);
            head.u32(sent_);
            if (!host_.send_data_(Message::update_data, head, data + written, chunk))
                break;
            written += chunk;
            sent_ += static_cast<std::uint32_t>(chunk);
        }
        return Status::ok;
    }

    Status HostFirmwareUpdate::finish()
    {
        host_.poll();
        switch (state_)
        {
            case State::failed:
                return failure_;
            case State::finished:
                return Status::ok;
            case State::finishing:
                return Status::busy;
            case State::writing:
            {
                if (sent_ != size_)
                    return Status::invalid_argument;
                if (written_ != size_)
                    return Status::busy;  // the device still writes what was sent
                std::uint8_t bytes[kHeadBytes];
                PacketWriter writer(bytes, sizeof(bytes));
                writer.u8(number_);
                if (host_.queue_(Message::update_finish, writer))
                    state_ = State::finishing;
                return Status::busy;
            }
            default:
                return Status::not_ready;
        }
    }

    void HostFirmwareUpdate::abort()
    {
        if (running_())
        {
            std::uint8_t bytes[kHeadBytes];
            PacketWriter writer(bytes, sizeof(bytes));
            writer.u8(number_);
            (void)host_.queue_(Message::update_abort, writer);
        }
        state_ = State::idle;
    }

    Status HostFirmwareUpdate::restart()
    {
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        return host_.queue_(Message::restart, writer) ? Status::ok : Status::busy;
    }

}  // namespace remote

// ------------------------------------------------------------ RemoteNetworkHost

RemoteNetworkHost::RemoteNetworkHost(RemoteNetworkHostConfig config)
    : config_(config), link_(config.link), station_(*this), access_point_(*this), dns_(*this), listener_(*this),
      udp_(*this), update_(*this)
{
    for (std::size_t index = 0; index < remote::kTcpSlots; ++index)
    {
        sockets_[index].host_  = this;
        sockets_[index].index_ = static_cast<std::uint8_t>(index);
    }
}

Status RemoteNetworkHost::begin()
{
    if (config_.link.memory == nullptr)
        return Status::invalid_argument;
    using brick::interfaces::memory::MemoryKind;
    for (std::size_t index = 0; index < remote::kTcpSlots; ++index)
    {
        remote::HostTcpSocket& socket = sockets_[index];
        if (socket.ring_.capacity() != 0)
            continue;
        const std::size_t bytes =
            index < remote::kClientSlots ? config_.client_receive_bytes[index] : config_.accepted_receive_bytes;
        if (!socket.ring_.attach(
                static_cast<std::uint8_t*>(config_.link.memory->allocate(bytes, MemoryKind::internal, sizeof(void*))),
                bytes))
            return Status::no_memory;
    }
    if (udp_.ring_.capacity() == 0
        && !udp_.ring_.attach(static_cast<std::uint8_t*>(config_.link.memory->allocate(
                                  config_.udp_receive_bytes, MemoryKind::internal, sizeof(void*))),
                              config_.udp_receive_bytes))
        return Status::no_memory;
    return link_.begin(*this);
}

void RemoteNetworkHost::poll()
{
    if (polling_)
        return;
    polling_ = true;
    link_.poll();
    if (link_.connected() && link_.quiet_ms() > config_.device_timeout_ms)
    {
        // The device stopped answering: sync again; it starts over when it answers.
        link_.restart();
        device_ready_ = false;
    }
    flush_();
    link_.poll();
    polling_ = false;
}

bool RemoteNetworkHost::queue_(Message message, const PacketWriter& writer)
{
    if (!writer.ok() || writer.size() > kQueueBytes || queue_count_ == kQueueEntries)
    {
        ++overflows_;
        return false;
    }
    Queued& entry = queue_entries_[(queue_head_ + queue_count_) % kQueueEntries];
    entry.type    = static_cast<std::uint8_t>(message);
    entry.size    = static_cast<std::uint8_t>(writer.size());
    std::memcpy(entry.payload.data(), writer.data(), writer.size());
    ++queue_count_;
    flush_();
    return true;
}

void RemoteNetworkHost::flush_()
{
    while (queue_count_ != 0 && link_.room() != 0)
    {
        const Queued& entry = queue_entries_[queue_head_];
        if (!link_.send(entry.type, entry.payload.data(), entry.size))
            return;
        queue_head_ = (queue_head_ + 1U) % kQueueEntries;
        --queue_count_;
    }
}

bool RemoteNetworkHost::data_room_() const
{
    return queue_count_ == 0 && link_.room() > kControlReserve;
}

bool RemoteNetworkHost::send_data_(Message message, const PacketWriter& head, const std::uint8_t* data,
                                   std::size_t size)
{
    return data_room_() && head.ok()
           && link_.send(static_cast<std::uint8_t>(message), head.data(), head.size(), data, size);
}

void RemoteNetworkHost::on_link_reset()
{
    device_ready_ = false;
    queue_count_  = 0;  // what waited was for the device before
    for (remote::HostTcpSocket& socket : sockets_)
    {
        if (socket.state_ != remote::HostTcpSocket::State::closed)
        {
            socket.state_   = remote::HostTcpSocket::State::failed;
            socket.failure_ = Status::error;
        }
        socket.waiting_ = false;
    }
    if (dns_.state_ == Status::busy)
        dns_.state_ = Status::timeout;
    if (station_.scan_ == Status::busy)
        station_.scan_ = Status::error;
    if (station_.link_ != LinkState::down)
        station_.failure_ = WifiFailure::connection_lost;
    station_.link_         = LinkState::down;
    station_.joined_       = false;
    station_.request_      = 0;
    access_point_.link_    = LinkState::down;
    access_point_.clients_ = 0;
    access_point_.request_ = 0;
    if (update_.running_())
    {
        update_.state_   = remote::HostFirmwareUpdate::State::failed;  // the device started over in the middle
        update_.failure_ = Status::error;
    }
    update_.description_[0] = '\0';

    std::uint8_t bytes[kHeadBytes];
    PacketWriter hello(bytes, sizeof(bytes));
    hello.u8(remote::kProtocolVersion);
    (void)queue_(Message::hello, hello);
    if (access_point_.started_)
        access_point_.queue_start_();
    if (listener_.listening_)
    {
        PacketWriter listen(bytes, sizeof(bytes));
        listen.u16(listener_.port_);
        listen.u32(static_cast<std::uint32_t>(config_.accepted_receive_bytes));
        (void)queue_(Message::listen, listen);
    }
    if (udp_.open_)
    {
        PacketWriter open(bytes, sizeof(bytes));
        open.u16(udp_.port_);
        (void)queue_(Message::udp_open, open);
    }
}

void RemoteNetworkHost::on_packet(std::uint8_t type, const std::uint8_t* payload, std::size_t size)
{
    PacketReader  reader(payload, size);
    const Message message = static_cast<Message>(type);
    switch (message)
    {
        case Message::state:
            on_state_(reader);
            break;
        case Message::scan_done:
            on_scan_(reader);
            break;
        case Message::dns_result:
        {
            const std::uint8_t lookup  = reader.u8();
            const Status       status  = reader.status();
            const auto         address = reader.address();
            if (reader.ok() && lookup == dns_.lookup_ && dns_.state_ == Status::busy)
            {
                dns_.state_   = status;
                dns_.address_ = address;
            }
            break;
        }
        case Message::udp_data:
            on_udp_(reader);
            break;
        case Message::update_state:
            on_update_(reader);
            break;
        case Message::description:
            reader.text(update_.description_, sizeof(update_.description_));
            break;
        case Message::tcp_status:
        case Message::tcp_data:
        case Message::tcp_sent:
        case Message::tcp_closed:
        case Message::tcp_accepted:
            on_tcp_(message, reader);
            break;
        default:
            break;
    }
}

void RemoteNetworkHost::on_update_(PacketReader& reader)
{
    using State                 = remote::HostFirmwareUpdate::State;
    const std::uint8_t  number  = reader.u8();
    const auto          phase   = static_cast<remote::UpdatePhase>(reader.u8());
    const Status        status  = reader.status();
    const std::uint32_t written = reader.u32();
    if (!reader.ok() || number != update_.number_ || !update_.running_())
        return;
    switch (phase)
    {
        case remote::UpdatePhase::writing:
        case remote::UpdatePhase::checking:
            if (update_.state_ == State::starting)
                update_.state_ = State::writing;
            if (written <= update_.sent_)
                update_.written_ = written;
            break;
        case remote::UpdatePhase::finished:
            if (update_.state_ == State::finishing)
                update_.state_ = State::finished;
            break;
        case remote::UpdatePhase::failed:
            update_.state_   = State::failed;
            update_.failure_ = is_ok(status) ? Status::error : status;
            break;
        default:
            break;
    }
}

void RemoteNetworkHost::on_state_(PacketReader& reader)
{
    remote::RemoteState state;
    state.read(reader);
    if (!reader.ok())
        return;
    device_ready_      = true;
    station_.mac_      = state.station_mac;
    access_point_.mac_ = state.ap_mac;
    // Only a state that has seen the last request; an older one would undo it.
    if (state.station_request == station_.request_)
    {
        station_.link_    = state.station_link;
        station_.failure_ = state.station_failure;
        station_.ip_      = state.station_ip;
        station_.joined_  = state.joined;
        station_.network_ = state.network;
    }
    if (state.ap_request == access_point_.request_)
    {
        access_point_.link_    = state.ap_link;
        access_point_.ip_      = state.ap_ip;
        access_point_.clients_ = state.ap_clients;
    }
}

void RemoteNetworkHost::on_scan_(PacketReader& reader)
{
    const Status      status = reader.status();
    const std::size_t count  = std::min<std::size_t>(reader.u8(), remote::kMaxScanResults);
    for (std::size_t index = 0; index < count; ++index)
        station_.scan_results_[index] = reader.network();
    if (!reader.ok() || station_.scan_ != Status::busy)
        return;
    station_.scan_       = status;
    station_.scan_count_ = is_ok(status) ? count : 0U;
}

void RemoteNetworkHost::on_tcp_(Message message, PacketReader& reader)
{
    const std::size_t  index      = reader.u8();
    const std::uint8_t generation = reader.u8();
    if (!reader.ok() || index >= remote::kTcpSlots)
        return;
    remote::HostTcpSocket& socket = sockets_[index];
    using State                   = remote::HostTcpSocket::State;
    if (message == Message::tcp_accepted)
    {
        if (index >= remote::kClientSlots && listener_.listening_)
        {
            socket.opened_(generation);
            socket.waiting_ = true;
        }
        return;
    }
    if (generation != socket.generation_)
        return;  // for a connection closed since
    switch (message)
    {
        case Message::tcp_status:
        {
            const Status status = reader.status();
            if (socket.state_ != State::connecting)
                return;
            socket.state_   = is_ok(status) ? State::connected : State::failed;
            socket.failure_ = status == Status::timeout ? Status::timeout : Status::error;
            return;
        }
        case Message::tcp_data:
            if (socket.state_ == State::connected && !socket.ring_.write(reader.rest(), reader.remaining()))
                ++overflows_;
            return;
        case Message::tcp_sent:
            socket.send_credit_ = std::min<std::uint32_t>(socket.send_credit_ + reader.u32(),
                                                          static_cast<std::uint32_t>(remote::kDeviceSendBytes));
            return;
        case Message::tcp_closed:
        {
            const Status status = reader.status();
            if (socket.state_ == State::connected || socket.state_ == State::connecting)
                socket.state_ = status == Status::not_ready ? State::peer_closed : State::failed;
            return;
        }
        default:
            return;
    }
}

void RemoteNetworkHost::on_udp_(PacketReader& reader)
{
    const auto          address = reader.address();
    const std::uint16_t port    = reader.u16();
    const std::size_t   size    = reader.remaining();
    if (!reader.ok() || !udp_.open_)
        return;
    if (udp_.ring_.room() < kUdpRecordHeader + size)
    {
        ++overflows_;
        return;
    }
    std::uint8_t header[kUdpRecordHeader];
    PacketWriter writer(header, sizeof(header));
    writer.u16(static_cast<std::uint16_t>(size));
    writer.address(address);
    writer.u16(port);
    (void)udp_.ring_.write(header, sizeof(header));
    (void)udp_.ring_.write(reader.rest(), size);
}

}  // namespace brick::core::network
