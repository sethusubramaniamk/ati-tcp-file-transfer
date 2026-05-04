#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ftx/proto/types.hpp"

namespace ftx::proto {

// 32-byte hash digest (BLAKE3 in phase 3+; zeros in phase 2).
using Hash = std::array<std::byte, 32>;

inline constexpr Hash kZeroHash{};

// ---------------------------------------------------------------------------
// Message struct definitions. Each message corresponds to one FrameType. The
// encode_<msg> helpers return the *payload* bytes (no frame header); pair them
// with encode_frame() to put one on the wire.
// ---------------------------------------------------------------------------

struct HelloMsg {
    uint8_t  protocol_version = kProtocolVersion;
    uint32_t max_chunk_size   = 1u * 1024 * 1024;
    uint32_t capabilities     = 0;
};

// Sender → receiver. Describes the file being transferred.
struct ManifestMsg {
    uint64_t          file_size   = 0;
    uint32_t          chunk_size  = 0;
    uint32_t          chunk_count = 0;
    Hash              root_hash{};               // Merkle root over chunk_hashes
    std::string       path;                       // relative to receiver's --root
    std::vector<Hash> chunk_hashes;               // size == chunk_count
};

// Receiver → sender. Subset of chunk indices that the receiver still needs.
// On a fresh transfer this is the full range [0, chunk_count). On resume it
// is only the chunks missing from .ftxstate.
struct ReqChunksMsg {
    std::vector<uint32_t> indices;
};

struct ChunkMsg {
    uint32_t               index = 0;
    Hash                   hash{};                // BLAKE3(data) — verified by receiver
    std::vector<std::byte> data;
};

struct CompleteMsg {
    Hash    final_root_hash{};
    uint8_t status = 0;  // 0 = OK, non-zero = sender-side issue
};

struct AckMsg {
    uint32_t last_index = 0;
};

struct ErrorMsg {
    ErrorCode   code = ErrorCode::Unspecified;
    std::string message;
};

// ---------------------------------------------------------------------------
// Encoders — produce payload bytes ready to feed encode_frame().
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<std::byte> encode_hello      (const HelloMsg&      m);
[[nodiscard]] std::vector<std::byte> encode_manifest   (const ManifestMsg&   m);
[[nodiscard]] std::vector<std::byte> encode_req_chunks (const ReqChunksMsg&  m);
[[nodiscard]] std::vector<std::byte> encode_chunk      (const ChunkMsg&      m);
[[nodiscard]] std::vector<std::byte> encode_complete   (const CompleteMsg&   m);
[[nodiscard]] std::vector<std::byte> encode_ack        (const AckMsg&        m);
[[nodiscard]] std::vector<std::byte> encode_error      (const ErrorMsg&      m);

// ---------------------------------------------------------------------------
// Decoders — return nullopt if the payload is malformed (truncated, bad
// length prefix, etc.).
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<HelloMsg>      decode_hello      (std::span<const std::byte> payload);
[[nodiscard]] std::optional<ManifestMsg>   decode_manifest   (std::span<const std::byte> payload);
[[nodiscard]] std::optional<ReqChunksMsg>  decode_req_chunks (std::span<const std::byte> payload);
[[nodiscard]] std::optional<ChunkMsg>      decode_chunk      (std::span<const std::byte> payload);
[[nodiscard]] std::optional<CompleteMsg>   decode_complete   (std::span<const std::byte> payload);
[[nodiscard]] std::optional<AckMsg>        decode_ack        (std::span<const std::byte> payload);
[[nodiscard]] std::optional<ErrorMsg>      decode_error      (std::span<const std::byte> payload);

}  // namespace ftx::proto
