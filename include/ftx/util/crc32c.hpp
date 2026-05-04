#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ftx {

// CRC-32C (Castagnoli, polynomial 0x1EDC6F41 reflected as 0x82F63B78).
// Software implementation, table-driven, byte-at-a-time.
//
// The `seed` parameter allows chaining: crc32c(b, crc32c(a)) == crc32c(a||b).

[[nodiscard]] uint32_t crc32c(std::span<const std::byte> data, uint32_t seed = 0) noexcept;

}  // namespace ftx
