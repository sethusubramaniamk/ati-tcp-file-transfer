#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ftx {

// Big-endian (network byte order) read/write helpers over std::byte buffers.
// Fixed-extent spans so misuse is caught at compile time.

inline constexpr void write_be_u16(std::span<std::byte, 2> dst, uint16_t v) noexcept {
    dst[0] = std::byte{static_cast<unsigned char>((v >> 8) & 0xFFu)};
    dst[1] = std::byte{static_cast<unsigned char>(v & 0xFFu)};
}

inline constexpr void write_be_u32(std::span<std::byte, 4> dst, uint32_t v) noexcept {
    dst[0] = std::byte{static_cast<unsigned char>((v >> 24) & 0xFFu)};
    dst[1] = std::byte{static_cast<unsigned char>((v >> 16) & 0xFFu)};
    dst[2] = std::byte{static_cast<unsigned char>((v >> 8) & 0xFFu)};
    dst[3] = std::byte{static_cast<unsigned char>(v & 0xFFu)};
}

inline constexpr void write_be_u64(std::span<std::byte, 8> dst, uint64_t v) noexcept {
    for (size_t i = 0; i < 8; ++i) {
        dst[i] = std::byte{static_cast<unsigned char>((v >> (56u - 8u * i)) & 0xFFu)};
    }
}

inline constexpr uint16_t read_be_u16(std::span<const std::byte, 2> src) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(std::to_integer<uint8_t>(src[0])) << 8) |
                                 static_cast<uint16_t>(std::to_integer<uint8_t>(src[1])));
}

inline constexpr uint32_t read_be_u32(std::span<const std::byte, 4> src) noexcept {
    return (static_cast<uint32_t>(std::to_integer<uint8_t>(src[0])) << 24) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(src[1])) << 16) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(src[2])) << 8) |
           static_cast<uint32_t>(std::to_integer<uint8_t>(src[3]));
}

inline constexpr uint64_t read_be_u64(std::span<const std::byte, 8> src) noexcept {
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<uint64_t>(std::to_integer<uint8_t>(src[i]));
    }
    return v;
}

}  // namespace ftx
