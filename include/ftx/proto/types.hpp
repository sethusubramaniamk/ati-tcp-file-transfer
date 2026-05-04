#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ftx::proto {

// ftx wire-protocol primitives.
//
// Frame layout (big-endian):
//   bytes [0..4)   payload_len   uint32_t
//   byte  [4]      type          uint8_t  (FrameType)
//   bytes [5..9)   header_crc32c uint32_t  (CRC-32C over bytes [0..5))
//   bytes [9..)    payload       opaque, length == payload_len
//
// Payload framing is per-message-type and is defined in `messages.hpp`
// (added in subsequent phases).

enum class FrameType : uint8_t {
    Hello = 0x01,      ///< version negotiation, capabilities advertisement
    Manifest = 0x02,   ///< file metadata + per-chunk hash list + Merkle root
    ReqChunks = 0x03,  ///< receiver requests specific chunk indices (resume)
    Chunk = 0x04,      ///< sender delivers one chunk's payload
    Ack = 0x05,        ///< receiver acknowledges chunks
    Complete = 0x06,   ///< sender signals end-of-transfer with final root hash
    Error = 0xFF,      ///< structured error, terminates the session
};

[[nodiscard]] bool is_known_frame_type(uint8_t v) noexcept;
[[nodiscard]] std::string_view to_string(FrameType t) noexcept;

inline constexpr uint8_t kProtocolVersion = 1;

inline constexpr size_t kHeaderSize = 9;
inline constexpr size_t kDefaultMaxPayload = static_cast<size_t>(64) * 1024 * 1024;  // 64 MiB

// Application-layer error codes carried inside ERROR frames.
enum class ErrorCode : uint16_t {
    Unspecified = 0x0000,
    ProtocolViolation = 0x0001,
    UnsupportedVersion = 0x0002,
    PayloadTooLarge = 0x0003,
    HashMismatch = 0x0004,
    InvalidPath = 0x0005,
    NotFound = 0x0006,
    PermissionDenied = 0x0007,
    InternalError = 0x00FF,
};

}  // namespace ftx::proto
