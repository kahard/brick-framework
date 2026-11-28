#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/interfaces/communication/ISerialPort.h"
#include "brick/interfaces/memory/IMemoryProvider.h"
#include "brick/interfaces/time/ITimeProvider.h"

namespace brick::core::network
{

/// Takes the packets of a SerialPacketLink: in order, each once.
class IPacketSink
{
public:
    virtual ~IPacketSink() = default;

    virtual void on_packet(std::uint8_t type, const std::uint8_t* payload, std::size_t size) = 0;
    /// The link started over (the other end restarted or this one did): what
    /// was on its way is gone, and the other end knows nothing of before.
    virtual void on_link_reset() = 0;
};

struct SerialPacketLinkConfig
{
    brick::interfaces::communication::ISerialPort* port = nullptr;
    brick::interfaces::time::ITimeProvider*        time = nullptr;
    /// The window's memory (window_packets x kMaxPayload) comes from here.
    brick::interfaces::memory::IMemoryProvider* memory = nullptr;
    /// Packets sent and not yet confirmed: a power of two up to kMaxWindow.
    /// The other end confirms only when it polls, so the window should hold
    /// what the line carries while it is busy elsewhere.
    std::size_t window_packets = 8;
    /// Packets that arrive after a lost one are kept until it comes again:
    /// as many as the other end's window (a power of two up to kMaxWindow).
    std::size_t receive_packets = 8;
    /// Packets not confirmed in this time are sent again, and a missing one
    /// is asked for again after half of it. Several times what a full window
    /// takes on the line and the peer to answer: 200 ms suits a slow line, a
    /// fast one recovers sooner with less.
    std::uint32_t retransmit_ms = 200;
    /// Until the other end answers, a sync goes out this often.
    std::uint32_t sync_ms = 200;
};

/// Reliable packets over a serial port, the same at both ends. Each packet is
/// one frame: SLIP (RFC 1055) around a header, the payload and a CRC-16.
/// Frames carry a sequence number and confirm what came from the other end.
/// A frame lost or damaged is sent again on its own (selective repeat): the
/// receiver keeps the ones that come after it and asks for the missing one
/// (a NAK) as soon as the next frame shows the gap; what is not confirmed
/// within retransmit_ms goes again too. So packets arrive in order and once.
/// A sync handshake starts both ends at zero; either end may sync again at
/// any time (a restart).
/// Nothing blocks: poll() reads, delivers and sends what fits.
class SerialPacketLink final
{
public:
    static constexpr std::size_t kMaxPayload = 1024;
    static constexpr std::size_t kMaxWindow  = 64;
    /// Packet types below this are the link's own.
    static constexpr std::uint8_t kFirstType = 8;

    explicit SerialPacketLink(SerialPacketLinkConfig config);

    /// Takes the window's memory and starts the handshake.
    Status begin(IPacketSink& sink);
    void   poll();

    /// The handshake is done: packets can be sent.
    bool connected() const { return connected_; }
    /// Packets send() takes now.
    std::size_t room() const;
    /// Queues a packet of head and data (either may be empty); false when not
    /// connected, the window is full or the payload is over kMaxPayload.
    bool send(std::uint8_t type, const std::uint8_t* head, std::size_t head_size, const std::uint8_t* data = nullptr,
              std::size_t data_size = 0);
    /// Starts over: the handshake again, the window emptied.
    void restart();

    /// Milliseconds since the last frame came from the other end.
    std::uint32_t quiet_ms() const;
    /// Frames dropped for a wrong CRC or length, packets sent again.
    std::uint32_t damaged_frames() const { return damaged_; }
    std::uint32_t retransmissions() const { return retransmissions_; }
    /// Oldest unconfirmed, next queued, next to send and next expected
    /// sequence, one byte each from the lowest: for a log.
    std::uint32_t sequences() const
    {
        return static_cast<std::uint32_t>(base_) | (static_cast<std::uint32_t>(next_) << 8U)
               | (static_cast<std::uint32_t>(send_next_) << 16U) | (static_cast<std::uint32_t>(expected_) << 24U);
    }

private:
    enum class Control : std::uint8_t
    {
        data,
        ack,
        sync,
        sync_ack,
        nak,  ///< the packet of this sequence is missing: send it again now
    };

    static constexpr std::size_t kHeader     = 4;  // control, sequence, acknowledge, type
    static constexpr std::size_t kCrc        = 2;
    static constexpr std::size_t kFrame      = kHeader + kMaxPayload + kCrc;
    static constexpr std::size_t kEncoded    = 2 * kFrame + 2;  // every byte escaped, two ENDs
    static constexpr std::size_t kReadBlock  = 512;
    static constexpr std::size_t kReadBudget = 32 * 1024;

    void         reset_();
    void         read_();
    void         accept_(const std::uint8_t* frame, std::size_t size);
    void         acknowledge_(std::uint8_t ack);
    void         keep_(std::uint8_t sequence, std::uint8_t type, const std::uint8_t* payload, std::size_t size);
    void         deliver_kept_();
    void         ask_again_(std::uint8_t sequence);
    bool         encode_repeat_();
    void         write_();
    bool         encode_next_();
    void         encode_(Control control, std::uint8_t sequence, std::uint8_t type, const std::uint8_t* payload,
                         std::size_t size);
    std::uint8_t in_flight_() const;

    SerialPacketLinkConfig                config_;
    IPacketSink*                          sink_   = nullptr;
    std::uint8_t*                         window_ = nullptr;
    std::array<std::uint16_t, kMaxWindow> sizes_{};
    std::array<std::uint8_t, kMaxWindow>  types_{};
    std::size_t                           window_mask_ = 0;
    std::uint64_t                         repeat_      = 0;  ///< window slots to send again
    // Packets that came after a missing one, by sequence & receive_mask_.
    std::uint8_t*                         kept_ = nullptr;
    std::array<std::uint16_t, kMaxWindow> kept_sizes_{};
    std::array<std::uint8_t, kMaxWindow>  kept_types_{};
    std::uint64_t                         kept_present_ = 0;
    std::size_t                           receive_mask_ = 0;

    bool          connected_       = false;
    bool          sync_ack_due_    = false;
    bool          ack_due_         = false;
    bool          nak_due_         = false;
    std::uint8_t  nak_sequence_    = 0;  ///< the missing packet last asked for
    std::uint32_t nak_at_          = 0;
    std::uint32_t sync_sent_at_    = 0;
    bool          sync_sent_       = false;
    std::uint8_t  base_            = 0;  ///< oldest packet not confirmed
    std::uint8_t  next_            = 0;  ///< sequence of the next packet queued
    std::uint8_t  send_next_       = 0;  ///< next packet to put on the line
    std::uint8_t  expected_        = 0;  ///< next sequence from the other end
    std::uint32_t timer_from_      = 0;
    std::uint32_t last_heard_      = 0;
    std::uint32_t damaged_         = 0;
    std::uint32_t retransmissions_ = 0;

    std::array<std::uint8_t, kEncoded>   tx_{};
    std::size_t                          tx_length_ = 0;
    std::size_t                          tx_sent_   = 0;
    std::array<std::uint8_t, kFrame>     rx_{};
    std::size_t                          rx_length_   = 0;
    bool                                 rx_escape_   = false;
    bool                                 rx_overflow_ = false;
    std::array<std::uint8_t, kReadBlock> read_block_{};
};

}  // namespace brick::core::network
