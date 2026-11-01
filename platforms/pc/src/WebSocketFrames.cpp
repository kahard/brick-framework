#include "WebSocketFrames.h"

#include <array>

namespace brick::platform::pc::websocket
{

namespace
{
    constexpr char          kHandshakeGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    constexpr std::uint8_t  kFin             = 0x80;
    constexpr std::uint8_t  kMasked          = 0x80;
    constexpr std::uint8_t  kLength16        = 126;
    constexpr std::uint8_t  kLength64        = 127;
    constexpr std::size_t   kMaskBytes       = 4;
    constexpr std::uint32_t kSha1Start[]     = { 0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U };

    std::uint32_t rotate_left(std::uint32_t value, int bits)
    {
        return (value << bits) | (value >> (32 - bits));
    }

    void sha1_block(const unsigned char* block, std::array<std::uint32_t, 5>& state)
    {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0; index < 16; ++index)
            words[index] = static_cast<std::uint32_t>(block[index * 4]) << 24
                           | static_cast<std::uint32_t>(block[index * 4 + 1]) << 16
                           | static_cast<std::uint32_t>(block[index * 4 + 2]) << 8 | block[index * 4 + 3];
        for (std::size_t index = 16; index < words.size(); ++index)
            words[index] = rotate_left(words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            std::uint32_t mix      = 0;
            std::uint32_t constant = 0;
            if (index < 20)
                mix = (b & c) | (~b & d), constant = 0x5A827999U;
            else if (index < 40)
                mix = b ^ c ^ d, constant = 0x6ED9EBA1U;
            else if (index < 60)
                mix = (b & c) | (b & d) | (c & d), constant = 0x8F1BBCDCU;
            else
                mix = b ^ c ^ d, constant = 0xCA62C1D6U;
            const std::uint32_t next = rotate_left(a, 5) + mix + e + constant + words[index];
            e                        = d;
            d                        = c;
            c                        = rotate_left(b, 30);
            b                        = a;
            a                        = next;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    void append_length(std::string& out, std::uint64_t length, std::size_t bytes)
    {
        for (std::size_t index = bytes; index-- > 0;)
            out += static_cast<char>((length >> (index * 8)) & 0xFFU);
    }
}  // namespace

std::string sha1(const std::string& text)
{
    std::string message = text;
    message += static_cast<char>(0x80);
    while (message.size() % 64 != 56)
        message += '\0';
    append_length(message, static_cast<std::uint64_t>(text.size()) * 8, 8);
    std::array<std::uint32_t, 5> state{ kSha1Start[0], kSha1Start[1], kSha1Start[2], kSha1Start[3], kSha1Start[4] };
    for (std::size_t offset = 0; offset < message.size(); offset += 64)
        sha1_block(reinterpret_cast<const unsigned char*>(message.data()) + offset, state);
    std::string digest;
    for (const std::uint32_t word : state)
        append_length(digest, word, 4);
    return digest;
}

std::string base64(const std::string& bytes)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string           out;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3)
    {
        const std::size_t count = bytes.size() - offset < 3 ? bytes.size() - offset : 3;
        std::uint32_t     group = 0;
        for (std::size_t index = 0; index < 3; ++index)
            group = group << 8 | (index < count ? static_cast<unsigned char>(bytes[offset + index]) : 0U);
        for (std::size_t index = 0; index < 4; ++index)
            out += index <= count ? kAlphabet[(group >> (18 - 6 * index)) & 0x3FU] : '=';
    }
    return out;
}

std::string accept_key(const std::string& client_key)
{
    return base64(sha1(client_key + kHandshakeGuid));
}

void append_frame(std::string& out, Opcode opcode, const std::string& payload)
{
    out += static_cast<char>(kFin | static_cast<std::uint8_t>(opcode));
    if (payload.size() < kLength16)
    {
        out += static_cast<char>(payload.size());
    }
    else if (payload.size() <= 0xFFFFU)
    {
        out += static_cast<char>(kLength16);
        append_length(out, payload.size(), 2);
    }
    else
    {
        out += static_cast<char>(kLength64);
        append_length(out, payload.size(), 8);
    }
    out += payload;
}

bool take_frame(std::string& in, Frame& frame)
{
    if (in.size() < 2)
        return false;
    const auto        byte   = [&in](std::size_t index) { return static_cast<unsigned char>(in[index]); };
    std::size_t       offset = 2;
    std::uint64_t     length = byte(1) & 0x7FU;
    const std::size_t extra  = length == kLength16 ? 2 : length == kLength64 ? 8 : 0;
    if (in.size() < offset + extra)
        return false;
    if (extra > 0)
    {
        length = 0;
        for (std::size_t index = 0; index < extra; ++index)
            length = length << 8 | byte(offset + index);
        offset += extra;
    }
    const bool        masked = (byte(1) & kMasked) != 0;
    const std::size_t header = offset + (masked ? kMaskBytes : 0);
    if (in.size() < header || in.size() - header < length)
        return false;
    frame.opcode = static_cast<Opcode>(byte(0) & 0x0FU);
    frame.payload.assign(in, header, static_cast<std::size_t>(length));
    if (masked)
        for (std::size_t index = 0; index < frame.payload.size(); ++index)
            frame.payload[index] = static_cast<char>(frame.payload[index] ^ in[offset + index % kMaskBytes]);
    in.erase(0, header + static_cast<std::size_t>(length));
    return true;
}

}  // namespace brick::platform::pc::websocket
