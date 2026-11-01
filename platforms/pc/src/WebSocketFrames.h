#pragma once

// The small part of RFC 6455 (WebSocket) the web screen needs: the handshake
// key and the framing. Internal to platforms/pc.

#include <cstddef>
#include <cstdint>
#include <string>

namespace brick::platform::pc::websocket
{

enum class Opcode : std::uint8_t
{
    continuation = 0x0,
    text         = 0x1,
    binary       = 0x2,
    close        = 0x8,
    ping         = 0x9,
    pong         = 0xA,
};

/// Sec-WebSocket-Accept for a client's Sec-WebSocket-Key.
std::string accept_key(const std::string& client_key);

/// Appends one unmasked server frame (FIN set) carrying payload.
void append_frame(std::string& out, Opcode opcode, const std::string& payload);

/// A frame read from a client.
struct Frame
{
    Opcode      opcode = Opcode::text;
    std::string payload;  ///< unmasked
};

/// Takes the first complete frame off the front of in. False while in holds
/// only part of a frame.
bool take_frame(std::string& in, Frame& frame);

/// Base64 of bytes (used by accept_key, exposed for tests).
std::string base64(const std::string& bytes);
/// The 20-byte SHA-1 digest of text (used by accept_key, exposed for tests).
std::string sha1(const std::string& text);

}  // namespace brick::platform::pc::websocket
