#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ftx/proto/types.hpp"

namespace ftx::proto {

// Fixed 9-byte frame header.
struct FrameHeader {
    uint32_t  payload_len = 0;
    FrameType type        = FrameType::Hello;

    // Encode this header into exactly kHeaderSize bytes (including the trailing
    // CRC-32C over [length || type]).
    void encode_to(std::span<std::byte, kHeaderSize> dst) const noexcept;

    enum class DecodeError {
        BadCrc,
        UnknownType,
        PayloadTooLarge,
    };

    // Decode a header from exactly kHeaderSize bytes. Returns nullopt on:
    //   - CRC mismatch
    //   - type byte not in the FrameType enum
    //   - declared payload_len > max_payload
    // When err_out is non-null and decode fails, the reason is written there.
    [[nodiscard]] static std::optional<FrameHeader> decode_from(
        std::span<const std::byte, kHeaderSize> src,
        size_t                                  max_payload = kDefaultMaxPayload,
        DecodeError*                            err_out     = nullptr) noexcept;
};

// A fully-parsed frame, owning its payload buffer.
struct Frame {
    FrameHeader            header;
    std::vector<std::byte> payload;
};

// One-shot encoder. Produces header + payload in a single buffer.
// Throws std::invalid_argument when payload size exceeds kDefaultMaxPayload
// (or the maximum representable as uint32_t, whichever is smaller).
[[nodiscard]] std::vector<std::byte> encode_frame(
    FrameType                  type,
    std::span<const std::byte> payload);

}  // namespace ftx::proto
