#include "ftx/util/blake3.hpp"

#include <cstring>
#include <new>

extern "C" {
#include <blake3.h>
}

namespace ftx {

namespace {
inline blake3_hasher* as_hasher(std::byte* p) noexcept {
    // The state_ buffer is correctly aligned (alignof(8)) and large enough.
    // We treat it as an in-place blake3_hasher.
    return std::launder(reinterpret_cast<blake3_hasher*>(p));
}
inline const blake3_hasher* as_hasher(const std::byte* p) noexcept {
    return std::launder(reinterpret_cast<const blake3_hasher*>(p));
}
}  // namespace

static_assert(sizeof(blake3_hasher) <= 1912,
              "ftx::Blake3Hasher::state_ buffer is undersized for blake3_hasher");
static_assert(alignof(blake3_hasher) <= 8,
              "ftx::Blake3Hasher::state_ alignment is insufficient for blake3_hasher");

HashDigest blake3(std::span<const std::byte> data) noexcept {
    blake3_hasher h;
    blake3_hasher_init(&h);
    if (!data.empty()) {
        blake3_hasher_update(&h, data.data(), data.size());
    }
    HashDigest out{};
    blake3_hasher_finalize(&h, reinterpret_cast<uint8_t*>(out.data()), out.size());
    return out;
}

Blake3Hasher::Blake3Hasher() noexcept {
    blake3_hasher_init(as_hasher(state_.data()));
}

void Blake3Hasher::update(std::span<const std::byte> data) noexcept {
    if (data.empty()) return;
    blake3_hasher_update(as_hasher(state_.data()), data.data(), data.size());
}

HashDigest Blake3Hasher::finalize() const noexcept {
    HashDigest out{};
    blake3_hasher_finalize(as_hasher(state_.data()),
                           reinterpret_cast<uint8_t*>(out.data()), out.size());
    return out;
}

void Blake3Hasher::reset() noexcept {
    blake3_hasher_init(as_hasher(state_.data()));
}

}  // namespace ftx
