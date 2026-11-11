#include "brick/core/network/CaptiveDns.h"

namespace brick::core::network
{

namespace
{
    constexpr char          kTag[]        = "brick_captive_dns";
    constexpr std::size_t   kHeaderBytes  = 12;
    constexpr std::size_t   kAnswerBytes  = 16;  ///< name pointer, type, class, TTL, length, address
    constexpr std::uint16_t kTypeA        = 1;
    constexpr std::uint16_t kClassIn      = 1;
    constexpr std::uint8_t  kTtlSeconds   = 60;
    constexpr std::size_t   kMaxPerUpdate = 4;

    std::uint16_t read16(const std::uint8_t* bytes)
    {
        return static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]);
    }

    void write16(std::uint8_t* bytes, std::uint16_t value)
    {
        bytes[0] = static_cast<std::uint8_t>(value >> 8);
        bytes[1] = static_cast<std::uint8_t>(value);
    }
}  // namespace

std::size_t captive_dns_answer(std::uint8_t* message, std::size_t size, std::size_t capacity,
                               const interfaces::network::Ipv4Address& answer)
{
    if (size < kHeaderBytes)
        return 0;
    const std::uint16_t flags = read16(message + 2);
    // A standard query (QR 0, opcode 0) with one question.
    if ((flags & 0xF800U) != 0 || read16(message + 4) != 1)
        return 0;
    std::size_t offset = kHeaderBytes;
    for (;;)
    {
        if (offset >= size)
            return 0;
        const std::uint8_t label = message[offset];
        if (label == 0)
            break;
        if ((label & 0xC0U) != 0)
            return 0;  // no compression in a question
        offset += 1U + label;
    }
    const std::size_t question_end = offset + 1 + 4;
    if (question_end > size)
        return 0;
    const bool address_query = read16(message + offset + 1) == kTypeA && read16(message + offset + 3) == kClassIn;
    if (address_query && question_end + kAnswerBytes > capacity)
        return 0;

    // Response, authoritative, recursion desired copied, recursion available.
    write16(message + 2, static_cast<std::uint16_t>(0x8480U | (flags & 0x0100U)));
    write16(message + 6, address_query ? 1 : 0);
    write16(message + 8, 0);
    write16(message + 10, 0);  // additional records (EDNS) of the query are dropped
    if (!address_query)
        return question_end;
    std::uint8_t* const record = message + question_end;
    write16(record, 0xC000U | kHeaderBytes);  // the name of the question
    write16(record + 2, kTypeA);
    write16(record + 4, kClassIn);
    write16(record + 6, 0);
    write16(record + 8, kTtlSeconds);
    write16(record + 10, 4);
    for (std::size_t index = 0; index < 4; ++index)
        record[12 + index] = answer.octets[index];
    return question_end + kAnswerBytes;
}

CaptiveDns::CaptiveDns(interfaces::network::IUdpSocket& socket, interfaces::logging::ILogger& logger)
    : socket_(socket), logger_(logger)
{
}

Status CaptiveDns::start(const interfaces::network::Ipv4Address& address)
{
    stop();
    if (const Status status = socket_.open(kPort); !is_ok(status))
        return status;
    address_  = address;
    answered_ = 0;
    running_  = true;
    return Status::ok;
}

void CaptiveDns::stop()
{
    if (running_)
        socket_.close();
    running_ = false;
}

void CaptiveDns::update()
{
    if (!running_)
        return;
    for (std::size_t count = 0; count < kMaxPerUpdate; ++count)
    {
        std::size_t                   received = 0;
        interfaces::network::Endpoint from;
        if (!is_ok(socket_.receive_from(message_.data(), message_.size(), received, from)))
            return;
        const std::size_t size = captive_dns_answer(message_.data(), received, message_.size(), address_);
        if (size == 0)
            continue;
        if (const Status status = socket_.send_to(from, message_.data(), size); !is_ok(status))
        {
            logger_.warning(kTag, "Answer not sent: %s", to_string(status));
            return;
        }
        ++answered_;
    }
}

}  // namespace brick::core::network
