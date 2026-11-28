#include "brick/core/network/RemoteNetworkDevice.h"

#include <algorithm>
#include <cstring>

namespace brick::core::network
{

namespace
{
    using remote::Message;
    using remote::PacketReader;
    using remote::PacketWriter;

    constexpr char          kTag[]            = "remote_net";
    constexpr std::size_t   kHeadBytes        = 16;
    constexpr std::size_t   kControlReserve   = 4;  // packets kept for messages other than data
    constexpr std::uint32_t kStateCheckMs     = 50;
    constexpr std::uint32_t kSentReportBytes  = remote::kDeviceSendBytes / 4U;
    constexpr std::size_t   kMaxCredentialLen = 64;
    constexpr std::size_t   kMaxHostName      = 256;
    constexpr std::uint32_t kClosingTimeoutMs = 5000;  // a peer that takes nothing more
    constexpr std::uint32_t kRestartDelayMs   = 200;   // what was to be sent goes out first

    bool update_runs(remote::UpdatePhase phase)
    {
        return phase == remote::UpdatePhase::writing || phase == remote::UpdatePhase::checking;
    }
}  // namespace

RemoteNetworkDevice::RemoteNetworkDevice(RemoteNetworkDeviceConfig config) : config_(config), link_(config.link)
{
}

Status RemoteNetworkDevice::begin()
{
    if (config_.station == nullptr || config_.access_point == nullptr || config_.dns == nullptr
        || config_.listener == nullptr || config_.udp == nullptr || config_.link.memory == nullptr)
        return Status::invalid_argument;
    for (std::size_t index = 0; index < remote::kTcpSlots; ++index)
    {
        Slot& slot = slots_[index];
        if (index < remote::kClientSlots)
        {
            slot.socket = config_.clients[index];
            if (slot.socket == nullptr)
                return Status::invalid_argument;
        }
        if (slot.pending == nullptr)
        {
            slot.pending = static_cast<std::uint8_t*>(config_.link.memory->allocate(
                remote::kDeviceSendBytes, brick::interfaces::memory::MemoryKind::internal, sizeof(void*)));
            if (slot.pending == nullptr)
                return Status::no_memory;
        }
    }
    return link_.begin(*this);
}

void RemoteNetworkDevice::poll()
{
    link_.poll();
    if (link_.connected())
    {
        service_state_();
        service_scan_();
        service_dns_();
        service_listener_();
        service_tcp_();
        service_udp_();
        service_update_();
    }
    link_.poll();
}

bool RemoteNetworkDevice::send_(Message message, const PacketWriter& head, const std::uint8_t* data, std::size_t size)
{
    return head.ok() && link_.send(static_cast<std::uint8_t>(message), head.data(), head.size(), data, size);
}

void RemoteNetworkDevice::handle_update_(Message message, PacketReader& reader)
{
    if (message == Message::restart)
    {
        restart_due_  = true;
        restart_from_ = config_.link.time->millis();
        return;
    }
    const std::uint8_t number = reader.u8();
    if (message == Message::update_begin)
    {
        const std::uint32_t size = reader.u32();
        if (update_runs(update_phase_))
            config_.update->abort();  // the host starts over
        update_number_  = number;
        update_size_    = size;
        update_written_ = 0;
        update_status_  = !reader.ok()              ? Status::invalid_argument :
                          config_.update == nullptr ? Status::not_supported :
                                                      config_.update->begin(size);
        update_phase_   = is_ok(update_status_) ? remote::UpdatePhase::writing : remote::UpdatePhase::failed;
        update_due_     = true;
        if (is_ok(update_status_))
            note_("Update of the program begins: %u bytes", static_cast<unsigned>(size));
        else
            log_("Update of the program refused (status %u)", static_cast<unsigned>(update_status_));
        return;
    }
    if (number != update_number_ || !update_runs(update_phase_))
        return;  // of an update that has ended
    switch (message)
    {
        case Message::update_data:
        {
            const std::uint32_t offset = reader.u32();
            const std::size_t   size   = reader.remaining();
            if (update_phase_ != remote::UpdatePhase::writing || !reader.ok() || offset != update_written_
                || size > update_size_ - update_written_)
            {
                end_update_(Status::invalid_argument);
                break;
            }
            Status      status = Status::ok;
            std::size_t done   = 0;
            while (is_ok(status) && done < size)
            {
                std::size_t written = 0;
                status              = config_.update->write(reader.rest() + done, size - done, written);
                if (is_ok(status) && written == 0)
                    status = Status::busy;  // a device's own flash takes all it is given
                done += written;
            }
            update_written_ += static_cast<std::uint32_t>(done);
            if (is_ok(status))
                update_due_ = true;  // the host sends on as this reaches it
            else
                end_update_(status);
            break;
        }
        case Message::update_finish:
            if (update_phase_ == remote::UpdatePhase::writing)
            {
                if (update_written_ == update_size_)
                {
                    update_phase_ = remote::UpdatePhase::checking;  // service_update_() sees it through
                    update_due_   = true;
                }
                else
                    end_update_(Status::invalid_argument);
            }
            break;
        default:  // update_abort
            config_.update->abort();
            update_phase_  = remote::UpdatePhase::idle;
            update_status_ = Status::ok;
            update_due_    = true;
            break;
    }
}

/// A running update ends with an error; the program in use stays.
void RemoteNetworkDevice::end_update_(Status status)
{
    if (update_runs(update_phase_))
        config_.update->abort();
    update_phase_  = remote::UpdatePhase::failed;
    update_status_ = status;
    update_due_    = true;
    log_("Update of the program failed (status %u)", static_cast<unsigned>(status));
}

void RemoteNetworkDevice::service_update_()
{
    if (update_phase_ == remote::UpdatePhase::checking)
    {
        const Status status = config_.update->finish();
        if (is_ok(status))
        {
            update_phase_  = remote::UpdatePhase::finished;
            update_status_ = Status::ok;
            update_due_    = true;
            note_("Update of the program written: %u bytes", static_cast<unsigned>(update_written_));
        }
        else if (status != Status::busy)
        {
            end_update_(status);
        }
    }
    if (description_due_)
    {
        PacketWriter writer(message_.data(), message_.size());
        char         text[remote::kDescriptionBytes];
        std::strncpy(text, config_.update->description(), sizeof(text) - 1U);
        text[sizeof(text) - 1U] = '\0';
        writer.text(text);
        if (send_(Message::description, writer))
            description_due_ = false;
    }
    if (update_due_)
    {
        std::uint8_t bytes[kHeadBytes];
        PacketWriter writer(bytes, sizeof(bytes));
        writer.u8(update_number_);
        writer.u8(static_cast<std::uint8_t>(update_phase_));
        writer.status(update_status_);
        writer.u32(update_written_);
        if (send_(Message::update_state, writer))
            update_due_ = false;
    }
    if (restart_due_ && config_.link.time->millis() - restart_from_ >= kRestartDelayMs)
    {
        restart_due_ = false;
        if (config_.update != nullptr)
            (void)config_.update->restart();
    }
}

bool RemoteNetworkDevice::data_room_() const
{
    return link_.room() > kControlReserve;
}

void RemoteNetworkDevice::log_(const char* format, unsigned value) const
{
    if (config_.logger != nullptr)
        config_.logger->warning(kTag, format, value);
}

void RemoteNetworkDevice::note_(const char* format, unsigned value) const
{
    if (config_.logger != nullptr)
        config_.logger->info(kTag, format, value);
}

void RemoteNetworkDevice::on_link_reset()
{
    // The host started over: nothing it opened before is wanted any more.
    for (std::size_t index = 0; index < remote::kTcpSlots; ++index)
        close_slot_(index);
    config_.listener->close();
    config_.udp->close();
    config_.dns->cancel();
    if (config_.station->link_state() != brick::interfaces::network::LinkState::down)
        (void)config_.station->disconnect();
    if (config_.access_point->link_state() != brick::interfaces::network::LinkState::down)
        (void)config_.access_point->stop();
    if (update_runs(update_phase_))
        config_.update->abort();  // the host that sent it is gone
    update_phase_    = remote::UpdatePhase::idle;
    update_due_      = false;
    description_due_ = false;
    listening_       = false;
    udp_open_        = false;
    scanning_        = false;
    scan_due_        = false;
    dns_pending_     = false;
    dns_due_         = false;
    station_request_ = 0;
    ap_request_      = 0;
    state_due_       = true;
    last_state_size_ = 0;
}

void RemoteNetworkDevice::on_packet(std::uint8_t type, const std::uint8_t* payload, std::size_t size)
{
    PacketReader  reader(payload, size);
    const Message message = static_cast<Message>(type);
    switch (message)
    {
        case Message::wifi_scan:
        case Message::wifi_connect:
        case Message::wifi_disconnect:
            handle_station_(message, reader);
            break;
        case Message::ap_start:
        case Message::ap_stop:
            handle_access_point_(message, reader);
            break;
        case Message::tcp_connect:
        case Message::tcp_send:
        case Message::tcp_close:
        case Message::tcp_credit:
            handle_tcp_(message, reader);
            break;
        case Message::update_begin:
        case Message::update_data:
        case Message::update_finish:
        case Message::update_abort:
        case Message::restart:
            handle_update_(message, reader);
            break;
        default:
            handle_other_(message, reader);
            break;
    }
}

void RemoteNetworkDevice::handle_station_(Message message, PacketReader& reader)
{
    if (message == Message::wifi_scan)
    {
        scan_status_ = config_.station->start_scan();
        scanning_    = scan_status_ == Status::ok || scan_status_ == Status::busy;
        scan_due_    = !scanning_;
        scan_count_  = 0;
        return;
    }
    station_request_ = reader.u8();
    state_due_       = true;
    if (message == Message::wifi_disconnect)
    {
        (void)config_.station->disconnect();
        return;
    }
    const bool hidden = reader.u8() != 0;
    char       ssid[kMaxCredentialLen];
    char       password[kMaxCredentialLen + 1];
    reader.text(ssid, sizeof(ssid));
    reader.text(password, sizeof(password));
    if (!reader.ok())
        return;
    brick::interfaces::network::WifiCredentials credentials;
    credentials.ssid     = ssid;
    credentials.password = password;
    credentials.hidden   = hidden;
    if (const Status status = config_.station->connect(credentials); !is_ok(status))
        log_("Wi-Fi connect refused (%u)", static_cast<unsigned>(status));
}

void RemoteNetworkDevice::handle_access_point_(Message message, PacketReader& reader)
{
    ap_request_ = reader.u8();
    state_due_  = true;
    if (message == Message::ap_stop)
    {
        (void)config_.access_point->stop();
        return;
    }
    brick::interfaces::network::WifiAccessPointConfig settings;
    settings.channel     = reader.u8();
    settings.max_clients = reader.u8();
    settings.hidden      = reader.u8() != 0;
    char ssid[kMaxCredentialLen];
    char password[kMaxCredentialLen + 1];
    reader.text(ssid, sizeof(ssid));
    reader.text(password, sizeof(password));
    if (!reader.ok())
        return;
    settings.ssid     = ssid;
    settings.password = password;
    if (const Status status = config_.access_point->start(settings); !is_ok(status))
        log_("Hotspot start refused (%u)", static_cast<unsigned>(status));
}

RemoteNetworkDevice::Slot* RemoteNetworkDevice::slot_(PacketReader& reader, bool client_only, std::size_t& index)
{
    index                         = reader.u8();
    const std::uint8_t generation = reader.u8();
    if (!reader.ok() || index >= (client_only ? remote::kClientSlots : remote::kTcpSlots))
        return nullptr;
    Slot& slot = slots_[index];
    if (client_only)
    {
        slot.generation = generation;
        return &slot;
    }
    return slot.generation == generation && slot.socket != nullptr ? &slot : nullptr;
}

void RemoteNetworkDevice::handle_tcp_(Message message, PacketReader& reader)
{
    if (message == Message::tcp_connect)
    {
        std::size_t index = 0;
        Slot* const slot  = slot_(reader, true, index);
        if (slot == nullptr)
            return;
        brick::interfaces::network::Endpoint remote_end;
        remote_end.address = reader.address();
        remote_end.port    = reader.u16();
        slot->credit       = reader.u32();
        slot->socket->close();
        slot->pending_size = 0;
        slot->passed       = 0;
        slot->closed_due   = false;
        slot->accepted_due = false;
        slot->status       = slot->socket->connect(remote_end);
        slot->state        = is_ok(slot->status) ? SlotState::connecting : SlotState::closed;
        slot->status_due   = !is_ok(slot->status);
        return;
    }
    std::size_t index = 0;
    Slot* const slot  = slot_(reader, false, index);
    if (slot == nullptr)
        return;
    if (message == Message::tcp_close)
    {
        if (index >= remote::kClientSlots)
            note_("Connection %u closed by the host", static_cast<unsigned>(index));
        // What the host sent before its close still goes to the peer (the end
        // of a page); the socket closes once that has left.
        if (slot->state == SlotState::connected && slot->pending_size != 0)
        {
            slot->state        = SlotState::closing;
            slot->closing_from = config_.link.time->millis();
            slot->status_due   = false;
            slot->closed_due   = false;
            return;
        }
        close_slot_(index);
        return;
    }
    if (message == Message::tcp_credit)
    {
        slot->credit += reader.u32();
        return;
    }
    // tcp_send: the host's credit keeps it within the pending buffer.
    const std::size_t size = reader.remaining();
    if (slot->state != SlotState::connected)
        return;
    if (size > remote::kDeviceSendBytes - slot->pending_size)
    {
        log_("Send data over the credit on slot %u", static_cast<unsigned>(index));
        return;
    }
    std::memcpy(slot->pending + slot->pending_size, reader.rest(), size);
    slot->pending_size += size;
}

void RemoteNetworkDevice::handle_other_(Message message, PacketReader& reader)
{
    switch (message)
    {
        case Message::hello:
        {
            const std::uint8_t version = reader.u8();
            if (version != remote::kProtocolVersion)
                log_("Host speaks protocol version %u", version);
            state_due_       = true;
            description_due_ = config_.update != nullptr;
            break;
        }
        case Message::default_route:
            if (static_cast<remote::Interface>(reader.u8()) == remote::Interface::access_point)
                (void)config_.access_point->set_default_route();
            else
                (void)config_.station->set_default_route();
            break;
        case Message::dns_resolve:
        {
            dns_lookup_ = reader.u8();
            char name[kMaxHostName];
            reader.text(name, sizeof(name));
            dns_status_  = config_.dns->resolve(name);
            dns_pending_ = is_ok(dns_status_);
            dns_due_     = !dns_pending_;
            break;
        }
        case Message::dns_cancel:
            if (reader.u8() == dns_lookup_)
            {
                config_.dns->cancel();
                dns_pending_ = false;
                dns_due_     = false;
            }
            break;
        case Message::listen:
        {
            const std::uint16_t port = reader.u16();
            accepted_window_         = reader.u32();
            const Status status      = config_.listener->listen(port);
            listening_               = is_ok(status);
            if (listening_)
                note_("Listening on port %u", port);
            else
                log_("Listening on port %u failed", port);
            break;
        }
        case Message::listen_close:
            config_.listener->close();
            listening_ = false;
            for (std::size_t index = remote::kClientSlots; index < remote::kTcpSlots; ++index)
            {
                slots_[index].socket = nullptr;  // closed with the listener
                close_slot_(index);
            }
            break;
        case Message::udp_open:
        {
            const std::uint16_t port = reader.u16();
            udp_open_                = is_ok(config_.udp->open(port));
            if (udp_open_)
                note_("UDP port %u open", port);
            else
                log_("UDP port %u not opened", port);
            break;
        }
        case Message::udp_send:
        {
            brick::interfaces::network::Endpoint to;
            to.address = reader.address();
            to.port    = reader.u16();
            if (reader.ok() && udp_open_)
                (void)config_.udp->send_to(to, reader.rest(), reader.remaining());
            break;
        }
        case Message::udp_close:
            config_.udp->close();
            udp_open_ = false;
            break;
        default:
            log_("Unknown message %u", static_cast<unsigned>(message));
            break;
    }
}

void RemoteNetworkDevice::close_slot_(std::size_t index)
{
    Slot& slot = slots_[index];
    if (slot.socket != nullptr)
        slot.socket->close();
    if (index >= remote::kClientSlots)
        slot.socket = nullptr;  // the listener takes the connection back
    slot.state        = SlotState::idle;
    slot.pending_size = 0;
    slot.passed       = 0;
    slot.status_due   = false;
    slot.closed_due   = false;
    slot.accepted_due = false;
}

void RemoteNetworkDevice::service_tcp_()
{
    for (std::size_t index = 0; index < remote::kTcpSlots; ++index)
        service_slot_(index);
}

void RemoteNetworkDevice::service_slot_(std::size_t index)
{
    Slot& slot = slots_[index];
    if (slot.socket == nullptr)
        return;
    if (slot.state == SlotState::closing)
    {
        flush_closing_(index);
        return;
    }
    std::uint8_t head_bytes[kHeadBytes];
    if (slot.accepted_due)
    {
        PacketWriter head(head_bytes, sizeof(head_bytes));
        head.u8(static_cast<std::uint8_t>(index));
        head.u8(slot.generation);
        slot.accepted_due = !send_(Message::tcp_accepted, head);
        if (slot.accepted_due)
            return;
    }
    if (slot.state == SlotState::connecting)
    {
        const Status status = slot.socket->poll_connect();
        if (status != Status::busy)
        {
            slot.status     = status;
            slot.status_due = true;
            slot.state      = is_ok(status) ? SlotState::connected : SlotState::closed;
        }
    }
    if (slot.status_due)
    {
        PacketWriter head(head_bytes, sizeof(head_bytes));
        head.u8(static_cast<std::uint8_t>(index));
        head.u8(slot.generation);
        head.status(slot.status);
        slot.status_due = !send_(Message::tcp_status, head);
    }
    if (slot.state == SlotState::connected)
    {
        // What the host sent goes on to the peer.
        if (slot.pending_size != 0)
        {
            std::size_t  sent   = 0;
            const Status status = slot.socket->send(slot.pending, slot.pending_size, sent);
            if (is_ok(status) && sent != 0)
            {
                std::memmove(slot.pending, slot.pending + sent, slot.pending_size - sent);
                slot.pending_size -= sent;
                slot.passed += static_cast<std::uint32_t>(sent);
            }
            else if (status != Status::busy && !is_ok(status))
            {
                slot.status     = status;
                slot.closed_due = true;
                slot.state      = SlotState::closed;
            }
        }
        // What the peer sent goes to the host, as far as it has room.
        while (slot.state == SlotState::connected && slot.credit != 0 && data_room_())
        {
            // As many packets as the link takes now, read from the socket at once.
            const std::size_t packets  = std::min(link_.room() - kControlReserve, kReadPackets);
            const std::size_t want     = std::min<std::size_t>(slot.credit, packets * remote::kMaxDataBytes);
            std::size_t       received = 0;
            const Status      status   = slot.socket->receive(scratch_.data(), want, received);
            if (is_ok(status) && received != 0)
            {
                for (std::size_t sent = 0; sent < received; sent += remote::kMaxDataBytes)
                {
                    PacketWriter head(head_bytes, sizeof(head_bytes));
                    head.u8(static_cast<std::uint8_t>(index));
                    head.u8(slot.generation);
                    (void)send_(Message::tcp_data, head, scratch_.data() + sent,
                                std::min(received - sent, remote::kMaxDataBytes));
                }
                slot.credit -= static_cast<std::uint32_t>(received);
                continue;
            }
            if (status != Status::no_data && !(is_ok(status) && received == 0))
            {
                slot.status     = status;  // not_ready: closed by the peer; error: broken
                slot.closed_due = true;
                slot.state      = SlotState::closed;
            }
            break;
        }
    }
    if (slot.passed != 0 && (slot.passed >= kSentReportBytes || slot.pending_size == 0))
    {
        PacketWriter head(head_bytes, sizeof(head_bytes));
        head.u8(static_cast<std::uint8_t>(index));
        head.u8(slot.generation);
        head.u32(slot.passed);
        if (send_(Message::tcp_sent, head))
            slot.passed = 0;
    }
    if (slot.closed_due)
    {
        PacketWriter head(head_bytes, sizeof(head_bytes));
        head.u8(static_cast<std::uint8_t>(index));
        head.u8(slot.generation);
        head.status(slot.status);
        slot.closed_due = !send_(Message::tcp_closed, head);
    }
}

void RemoteNetworkDevice::flush_closing_(std::size_t index)
{
    Slot&        slot   = slots_[index];
    std::size_t  sent   = 0;
    const Status status = slot.socket->send(slot.pending, slot.pending_size, sent);
    if (is_ok(status) && sent != 0)
    {
        std::memmove(slot.pending, slot.pending + sent, slot.pending_size - sent);
        slot.pending_size -= sent;
    }
    const bool broken = !is_ok(status) && status != Status::busy;
    if (slot.pending_size == 0 || broken || config_.link.time->millis() - slot.closing_from >= kClosingTimeoutMs)
        close_slot_(index);
}

void RemoteNetworkDevice::service_listener_()
{
    if (!listening_)
        return;
    for (std::size_t index = remote::kClientSlots; index < remote::kTcpSlots; ++index)
    {
        Slot& slot = slots_[index];
        if (slot.socket != nullptr)
            continue;
        brick::interfaces::network::ITcpSocket* const socket = config_.listener->accept();
        if (socket == nullptr)
            return;
        slot.socket       = socket;
        slot.generation   = static_cast<std::uint8_t>(slot.generation + 1U);
        slot.state        = SlotState::connected;
        slot.credit       = accepted_window_;
        slot.pending_size = 0;
        slot.passed       = 0;
        slot.accepted_due = true;
        note_("Connection in on slot %u", static_cast<unsigned>(index));
    }
}

void RemoteNetworkDevice::service_udp_()
{
    while (udp_open_ && data_room_())
    {
        std::size_t                          received = 0;
        brick::interfaces::network::Endpoint from;
        if (!is_ok(config_.udp->receive_from(scratch_.data(), remote::kMaxDataBytes, received, from)))
            return;
        std::uint8_t head_bytes[kHeadBytes];
        PacketWriter head(head_bytes, sizeof(head_bytes));
        head.address(from.address);
        head.u16(from.port);
        (void)send_(Message::udp_data, head, scratch_.data(), received);
    }
}

void RemoteNetworkDevice::service_dns_()
{
    if (dns_pending_)
    {
        dns_status_ = config_.dns->result(dns_address_);
        if (dns_status_ != Status::busy)
        {
            dns_pending_ = false;
            dns_due_     = true;
        }
    }
    if (!dns_due_)
        return;
    std::uint8_t head_bytes[kHeadBytes];
    PacketWriter head(head_bytes, sizeof(head_bytes));
    head.u8(dns_lookup_);
    head.status(dns_status_);
    head.address(dns_address_);
    dns_due_ = !send_(Message::dns_result, head);
}

void RemoteNetworkDevice::service_scan_()
{
    if (scanning_)
    {
        scan_status_ = config_.station->scan_results(scan_results_.data(), scan_results_.size(), scan_count_);
        if (scan_status_ == Status::busy)
            return;
        scanning_ = false;
        scan_due_ = true;
    }
    if (!scan_due_)
        return;
    PacketWriter writer(message_.data(), message_.size());
    writer.status(scan_status_);
    const std::size_t count = is_ok(scan_status_) ? scan_count_ : 0U;
    writer.u8(static_cast<std::uint8_t>(count));
    for (std::size_t index = 0; index < count; ++index)
        writer.network(scan_results_[index]);
    scan_due_ = !send_(Message::scan_done, writer);
}

void RemoteNetworkDevice::service_state_()
{
    const std::uint32_t now = config_.link.time->millis();
    if (!state_due_ && now - state_checked_at_ < kStateCheckMs)
        return;
    state_checked_at_ = now;
    remote::RemoteState state;
    state.station_request = station_request_;
    state.station_link    = config_.station->link_state();
    state.station_failure = config_.station->failure();
    (void)config_.station->ip_info(state.station_ip);
    (void)config_.station->mac_address(state.station_mac);
    state.joined     = is_ok(config_.station->connected_network(state.network));
    state.ap_request = ap_request_;
    state.ap_link    = config_.access_point->link_state();
    (void)config_.access_point->ip_info(state.ap_ip);
    (void)config_.access_point->mac_address(state.ap_mac);
    state.ap_clients = static_cast<std::uint8_t>(std::min<std::size_t>(config_.access_point->client_count(), 255U));

    PacketWriter writer(message_.data(), message_.size());
    state.write(writer);
    const bool changed =
        writer.size() != last_state_size_ || std::memcmp(message_.data(), last_state_.data(), writer.size()) != 0;
    if (!state_due_ && !changed && now - state_sent_at_ < remote::kStateIntervalMs)
        return;
    if (!send_(Message::state, writer))
        return;
    std::memcpy(last_state_.data(), message_.data(), writer.size());
    last_state_size_ = writer.size();
    state_sent_at_   = now;
    state_due_       = false;
}

}  // namespace brick::core::network
