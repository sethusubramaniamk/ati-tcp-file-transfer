#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ftx {

// 32-byte digest. Same layout as proto::Hash; assignable across the boundary.
using HashDigest = std::array<std::byte, 32>;

inline constexpr HashDigest kZeroDigest{};

// One-shot BLAKE3 over a contiguous range.
[[nodiscard]] HashDigest blake3(std::span<const std::byte> data) noexcept;

// Incremental BLAKE3. Cheap to construct/reset; safe to compute several
// digests in succession.
class Blake3Hasher {
 public:
    Blake3Hasher() noexcept;
    Blake3Hasher(const Blake3Hasher&) = delete;
    Blake3Hasher& operator=(const Blake3Hasher&) = delete;
    Blake3Hasher(Blake3Hasher&&) noexcept = default;
    Blake3Hasher& operator=(Blake3Hasher&&) noexcept = default;
    ~Blake3Hasher() = default;

    void update(std::span<const std::byte> data) noexcept;
    [[nodiscard]] HashDigest finalize() const noexcept;
    void reset() noexcept;

 private:
    // Opaque storage that must accommodate a `blake3_hasher`. Defined in the
    // .cpp; we use the maximum reported size from BLAKE3 1.5.x. Verified via
    // a static_assert at compile time in blake3.cpp.
    alignas(8) std::array<std::byte, 1912> state_{};
};

}  // namespace ftx
