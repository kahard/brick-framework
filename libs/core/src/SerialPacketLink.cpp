#include "brick/core/network/SerialPacketLink.h"

#include <algorithm>
#include <cstring>

namespace brick::core::network
{

namespace
{
    // SLIP (RFC 1055).
    constexpr std::uint8_t kEnd        = 0xC0;
    constexpr std::uint8_t kEscape     = 0xDB;
    constexpr std::uint8_t kEscapedEnd = 0xDC;
    constexpr std::uint8_t kEscapedEsc = 0xDD;

    // CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF.
    constexpr std::uint16_t kCrcPolynomial = 0x1021;
    constexpr std::uint16_t kCrcInitial    = 0xFFFF;
    constexpr std::uint16_t kCrcTopBit     = 0x8000;
    constexpr unsigned      kBitsPerByte   = 8;
    constexpr std::size_t   kByteValues    = 256;
    constexpr std::uint8_t  kSequenceMask  = 0xFF;

    constexpr std::array<std::uint16_t, kByteValues> make_crc_table()
    {
        std::array<std::uint16_t, kByteValues> table{};
        for (std::size_t value = 0; value < kByteValues; ++value)
        {
            auto crc = static_cast<std::uint16_t>(value << kBitsPerByte);
            for (unsigned bit = 0; bit < kBitsPerByte; ++bit)
                crc = static_cast<std::uint16_t>((crc & kCrcTopBit) != 0 ? (crc << 1) ^ kCrcPolynomial : crc << 1);
            table[value] = crc;
        }
        return table;
    }

    constexpr std::array<std::uint16_t, kByteValues> kCrcTable = make_crc_table();

    std::uint16_t crc_update(std::uint16_t crc, const std::uint8_t* data, std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index)
            crc = static_cast<std::uint16_t>((crc << kBitsPerByte)
                                             ^ kCrcTable[((crc >> kBitsPerByte) ^ data[index]) & kSequenceMask]);
        return crc;
    }

    /// Copies bytes to out with the two special bytes escaped and adds them
    /// to the checksum, in one pass over them; returns the end of the copy.
    std::uint8_t* put_escaped(std::uint8_t* out, const std::uint8_t* bytes, std::size_t count, std::uint16_t& sum)
    {
        std::uint16_t crc = sum;
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::uint8_t byte = bytes[index];
            crc                     = static_cast<std::uint16_t>((crc << kBitsPerByte)
                                             ^ kCrcTable[((crc >> kBitsPerByte) ^ byte) & kSequenceMask]);
            if (byte == kEnd || byte == kEscape)
            {
                *out++ = kEscape;
                *out++ = byte == kEnd ? kEscapedEnd : kEscapedEsc;
            }
            else
            {
                *out++ = byte;
            }
        }
        sum = crc;
        return out;
    }

    bool power_of_two(std::size_t value)
    {
        return value != 0 && (value & (value - 1U)) == 0;
    }
}  // namespace

SerialPacketLink::SerialPacketLink(SerialPacketLinkConfig config) : config_(config)
{
}

Status SerialPacketLink::begin(IPacketSink& sink)
{
    if (config_.port == nullptr || config_.time == nullptr || config_.memory == nullptr
        || !power_of_two(config_.window_packets) || config_.window_packets > kMaxWindow
        || !power_of_two(config_.receive_packets) || config_.receive_packets > kMaxWindow)
        return Status::invalid_argument;
    using brick::interfaces::memory::MemoryKind;
    if (window_ == nullptr)
        window_ = static_cast<std::uint8_t*>(
            config_.memory->allocate(config_.window_packets * kMaxPayload, MemoryKind::internal, sizeof(void*)));
    if (kept_ == nullptr)
        kept_ = static_cast<std::uint8_t*>(
            config_.memory->allocate(config_.receive_packets * kMaxPayload, MemoryKind::internal, sizeof(void*)));
    if (window_ == nullptr || kept_ == nullptr)
        return Status::no_memory;
    window_mask_  = config_.window_packets - 1U;
    receive_mask_ = config_.receive_packets - 1U;
    sink_         = &sink;
    last_heard_   = config_.time->millis();
    restart();
    return Status::ok;
}

void SerialPacketLink::restart()
{
    connected_    = false;
    sync_sent_    = false;
    sync_ack_due_ = false;
    reset_();
}

void SerialPacketLink::reset_()
{
    base_         = 0;
    next_         = 0;
    send_next_    = 0;
    expected_     = 0;
    ack_due_      = false;
    nak_due_      = false;
    repeat_       = 0;
    kept_present_ = 0;
    // A frame cut off here ends at the next END and fails its CRC.
    tx_length_ = 0;
    tx_sent_   = 0;
}

void SerialPacketLink::poll()
{
    if (sink_ == nullptr)
        return;
    read_();
    const std::uint32_t now  = config_.time->millis();
    const auto          sent = static_cast<std::uint8_t>((send_next_ - base_) & kSequenceMask);
    if (connected_ && sent != 0 && now - timer_from_ >= config_.retransmit_ms)
    {
        // Nothing confirmed for a while: every packet on its way goes again.
        for (std::uint8_t index = 0; index < sent; ++index)
            repeat_ |= std::uint64_t{ 1 } << ((base_ + index) & window_mask_);
        timer_from_ = now;
        ++retransmissions_;
    }
    // A packet asked for again that has not come (the request or the packet
    // was damaged too): the peer may have filled its window and send nothing
    // more that would bring the question up, so it is asked from here.
    if (connected_ && kept_present_ != 0 && !nak_due_ && now - nak_at_ >= config_.retransmit_ms / 2U)
    {
        nak_due_      = true;
        nak_sequence_ = expected_;
        nak_at_       = now;
    }
    write_();
}

std::size_t SerialPacketLink::room() const
{
    return connected_ ? config_.window_packets - in_flight_() : 0U;
}

bool SerialPacketLink::send(std::uint8_t type, const std::uint8_t* head, std::size_t head_size,
                            const std::uint8_t* data, std::size_t data_size)
{
    if (room() == 0 || head_size + data_size > kMaxPayload || type < kFirstType || (head_size != 0 && head == nullptr)
        || (data_size != 0 && data == nullptr))
        return false;
    // The first packet after a quiet spell starts the retransmission timer afresh.
    if (in_flight_() == 0)
        timer_from_ = config_.time->millis();
    const std::size_t slot    = next_ & window_mask_;
    std::uint8_t*     payload = window_ + slot * kMaxPayload;
    if (head_size != 0)
        std::memcpy(payload, head, head_size);
    if (data_size != 0)
        std::memcpy(payload + head_size, data, data_size);
    sizes_[slot] = static_cast<std::uint16_t>(head_size + data_size);
    types_[slot] = type;
    repeat_ &= ~(std::uint64_t{ 1 } << slot);
    next_ = static_cast<std::uint8_t>(next_ + 1U);
    return true;
}

std::uint32_t SerialPacketLink::quiet_ms() const
{
    return config_.time->millis() - last_heard_;
}

std::uint8_t SerialPacketLink::in_flight_() const
{
    return static_cast<std::uint8_t>((next_ - base_) & kSequenceMask);
}

void SerialPacketLink::read_()
{
    std::size_t budget = kReadBudget;
    while (budget != 0)
    {
        const std::size_t count = config_.port->read(read_block_.data(), std::min(read_block_.size(), budget));
        if (count == 0)
            return;
        budget -= count;
        for (std::size_t index = 0; index < count; ++index)
        {
            std::uint8_t byte = read_block_[index];
            if (byte == kEnd)
            {
                if (rx_overflow_ || (rx_length_ != 0 && rx_length_ < kHeader + kCrc))
                    ++damaged_;
                else if (rx_length_ != 0)
                    accept_(rx_.data(), rx_length_);
                rx_length_   = 0;
                rx_escape_   = false;
                rx_overflow_ = false;
                continue;
            }
            if (rx_escape_)
            {
                rx_escape_ = false;
                byte       = byte == kEscapedEnd ? kEnd : (byte == kEscapedEsc ? kEscape : byte);
            }
            else if (byte == kEscape)
            {
                rx_escape_ = true;
                continue;
            }
            if (rx_length_ < rx_.size())
                rx_[rx_length_++] = byte;
            else
                rx_overflow_ = true;
        }
    }
}

void SerialPacketLink::accept_(const std::uint8_t* frame, std::size_t size)
{
    const std::size_t body     = size - kCrc;
    const auto        received = static_cast<std::uint16_t>(frame[body] | (frame[body + 1] << kBitsPerByte));
    if (crc_update(kCrcInitial, frame, body) != received)
    {
        ++damaged_;
        return;
    }
    last_heard_                 = config_.time->millis();
    const auto         control  = static_cast<Control>(frame[0]);
    const std::uint8_t sequence = frame[1];
    const std::uint8_t ack      = frame[2];
    switch (control)
    {
        case Control::sync:
            reset_();
            connected_    = true;
            sync_ack_due_ = true;
            sink_->on_link_reset();
            return;
        case Control::sync_ack:
            if (!connected_)
            {
                reset_();
                connected_ = true;
                sink_->on_link_reset();
            }
            return;
        case Control::ack:
            if (connected_)
                acknowledge_(ack);
            return;
        case Control::nak:
        {
            if (!connected_)
                return;
            acknowledge_(ack);
            const auto offset = static_cast<std::uint8_t>((sequence - base_) & kSequenceMask);
            if (offset < static_cast<std::uint8_t>((send_next_ - base_) & kSequenceMask))
            {
                repeat_ |= std::uint64_t{ 1 } << (sequence & window_mask_);
                ++retransmissions_;
            }
            return;
        }
        case Control::data:
        {
            if (!connected_)
                return;
            acknowledge_(ack);
            ack_due_          = true;  // also for a repeat or a gap: tells where this end is
            const auto offset = static_cast<std::uint8_t>((sequence - expected_) & kSequenceMask);
            if (offset >= config_.receive_packets)
                return;  // a repeat of one delivered, or beyond what is kept
            if (offset != 0)
            {
                // One before it was lost: keep this one and ask for that.
                keep_(sequence, frame[3], frame + kHeader, body - kHeader);
                ask_again_(expected_);
                return;
            }
            expected_ = static_cast<std::uint8_t>(expected_ + 1U);
            sink_->on_packet(frame[3], frame + kHeader, body - kHeader);
            deliver_kept_();
            return;
        }
    }
    ++damaged_;  // an unknown control byte
}

void SerialPacketLink::acknowledge_(std::uint8_t ack)
{
    const auto confirmed = static_cast<std::uint8_t>((ack - base_) & kSequenceMask);
    if (confirmed == 0 || confirmed > in_flight_())
        return;
    for (std::uint8_t index = 0; index < confirmed; ++index)
        repeat_ &= ~(std::uint64_t{ 1 } << ((base_ + index) & window_mask_));
    base_       = ack;
    timer_from_ = config_.time->millis();
    if (static_cast<std::uint8_t>((send_next_ - base_) & kSequenceMask) > in_flight_())
        send_next_ = base_;
}

void SerialPacketLink::keep_(std::uint8_t sequence, std::uint8_t type, const std::uint8_t* payload, std::size_t size)
{
    const std::size_t   slot = sequence & receive_mask_;
    const std::uint64_t bit  = std::uint64_t{ 1 } << slot;
    if ((kept_present_ & bit) != 0)
        return;
    if (size != 0)
        std::memcpy(kept_ + slot * kMaxPayload, payload, size);
    kept_sizes_[slot] = static_cast<std::uint16_t>(size);
    kept_types_[slot] = type;
    kept_present_ |= bit;
}

void SerialPacketLink::deliver_kept_()
{
    while (true)
    {
        const std::size_t   slot = expected_ & receive_mask_;
        const std::uint64_t bit  = std::uint64_t{ 1 } << slot;
        if ((kept_present_ & bit) == 0)
            break;
        kept_present_ &= ~bit;
        expected_ = static_cast<std::uint8_t>(expected_ + 1U);
        sink_->on_packet(kept_types_[slot], kept_ + slot * kMaxPayload, kept_sizes_[slot]);
    }
    if (kept_present_ != 0)
        ask_again_(expected_);  // another one missing further on
}

void SerialPacketLink::ask_again_(std::uint8_t sequence)
{
    // Once per missing packet; again if it has not come in half the retransmission time.
    const std::uint32_t now = config_.time->millis();
    if (sequence == nak_sequence_ && (nak_due_ || now - nak_at_ < config_.retransmit_ms / 2U))
        return;
    nak_due_      = true;
    nak_sequence_ = sequence;
    nak_at_       = now;
}

bool SerialPacketLink::encode_repeat_()
{
    const auto sent = static_cast<std::uint8_t>((send_next_ - base_) & kSequenceMask);
    for (std::uint8_t index = 0; index < sent && repeat_ != 0; ++index)
    {
        const auto          sequence = static_cast<std::uint8_t>(base_ + index);
        const std::size_t   slot     = sequence & window_mask_;
        const std::uint64_t bit      = std::uint64_t{ 1 } << slot;
        if ((repeat_ & bit) == 0)
            continue;
        repeat_ &= ~bit;
        if (index == 0)
            timer_from_ = config_.time->millis();
        encode_(Control::data, sequence, types_[slot], window_ + slot * kMaxPayload, sizes_[slot]);
        ack_due_ = false;
        return true;
    }
    repeat_ = 0;  // bits of slots no longer on their way
    return false;
}

void SerialPacketLink::write_()
{
    while (true)
    {
        if (tx_sent_ < tx_length_)
        {
            tx_sent_ += config_.port->write(tx_.data() + tx_sent_, tx_length_ - tx_sent_);
            if (tx_sent_ < tx_length_)
                return;
        }
        tx_length_ = 0;
        tx_sent_   = 0;
        if (!encode_next_())
            return;
    }
}

bool SerialPacketLink::encode_next_()
{
    const std::uint32_t now = config_.time->millis();
    if (sync_ack_due_)
    {
        sync_ack_due_ = false;
        encode_(Control::sync_ack, 0, 0, nullptr, 0);
        return true;
    }
    if (nak_due_ && connected_)
    {
        nak_due_ = false;
        ack_due_ = false;
        nak_at_  = now;
        encode_(Control::nak, nak_sequence_, 0, nullptr, 0);
        return true;
    }
    if (!connected_)
    {
        if (sync_sent_ && now - sync_sent_at_ < config_.sync_ms)
            return false;
        sync_sent_    = true;
        sync_sent_at_ = now;
        encode_(Control::sync, 0, 0, nullptr, 0);
        return true;
    }
    if (repeat_ != 0 && encode_repeat_())
        return true;
    if (send_next_ != next_)
    {
        const std::size_t slot = send_next_ & window_mask_;
        if (send_next_ == base_)
            timer_from_ = now;
        encode_(Control::data, send_next_, types_[slot], window_ + slot * kMaxPayload, sizes_[slot]);
        send_next_ = static_cast<std::uint8_t>(send_next_ + 1U);
        ack_due_   = false;  // the frame carries the acknowledgement
        return true;
    }
    if (ack_due_)
    {
        ack_due_ = false;
        encode_(Control::ack, 0, 0, nullptr, 0);
        return true;
    }
    return false;
}

void SerialPacketLink::encode_(Control control, std::uint8_t sequence, std::uint8_t type, const std::uint8_t* payload,
                               std::size_t size)
{
    const std::uint8_t header[kHeader] = { static_cast<std::uint8_t>(control), sequence, expected_, type };
    std::uint16_t      crc             = kCrcInitial;
    std::uint8_t*      out             = tx_.data();
    *out++                             = kEnd;
    out                                = put_escaped(out, header, kHeader, crc);
    out                                = put_escaped(out, payload, size, crc);
    const std::uint8_t trailer[kCrc]   = { static_cast<std::uint8_t>(crc & kSequenceMask),
                                           static_cast<std::uint8_t>(crc >> kBitsPerByte) };
    std::uint16_t      unused          = 0;
    out                                = put_escaped(out, trailer, kCrc, unused);
    *out++                             = kEnd;
    tx_length_                         = static_cast<std::size_t>(out - tx_.data());
    tx_sent_                           = 0;
}

}  // namespace brick::core::network
