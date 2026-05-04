#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "ftx/util/blake3.hpp"

namespace {

// Convert a hex string to a HashDigest.
ftx::HashDigest hex_to_digest(std::string_view hex) {
    ftx::HashDigest out{};
    for (size_t i = 0; i < out.size(); ++i) {
        const auto hi = hex[i * 2];
        const auto lo = hex[i * 2 + 1];
        const auto h  = static_cast<unsigned char>(
            (hi >= 'a') ? (hi - 'a' + 10) : (hi - '0'));
        const auto l  = static_cast<unsigned char>(
            (lo >= 'a') ? (lo - 'a' + 10) : (lo - '0'));
        out[i] = std::byte{static_cast<unsigned char>((h << 4) | l)};
    }
    return out;
}

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

// BLAKE3 reference test vectors:
//   empty string  → af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262
//   "abc"         → 6437b3ac38465133ffb63b75273a8db78e5...  (we use the official one)
//   "IETF"        → 83a2de1ee6f4e6ab686889248f4ec0cf4cc5709446a682ffd1cbb4d6165181e2

TEST(Blake3, EmptyDigest) {
    const auto got = ftx::blake3({});
    const auto expected = hex_to_digest(
        "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262");
    EXPECT_EQ(got, expected);
}

TEST(Blake3, IetfShortInput) {
    const auto in  = bytes_of("IETF");
    const auto got = ftx::blake3(in);
    const auto expected = hex_to_digest(
        "83a2de1ee6f4e6ab686889248f4ec0cf4cc5709446a682ffd1cbb4d6165181e2");
    EXPECT_EQ(got, expected);
}

TEST(Blake3, IncrementalEqualsOneShot) {
    std::vector<std::byte> v(257, std::byte{0x77});
    const auto             one_shot = ftx::blake3(v);

    ftx::Blake3Hasher h;
    // Feed in three uneven chunks.
    h.update(std::span<const std::byte>(v.data(),       100));
    h.update(std::span<const std::byte>(v.data() + 100,  57));
    h.update(std::span<const std::byte>(v.data() + 157, 100));
    EXPECT_EQ(h.finalize(), one_shot);
}

TEST(Blake3, ResetReturnsToInitialState) {
    ftx::Blake3Hasher h;
    const auto        empty_first = h.finalize();

    const std::vector<std::byte> v(64, std::byte{0xAA});
    h.update(v);
    h.reset();
    const auto empty_again = h.finalize();
    EXPECT_EQ(empty_first, empty_again);
}

TEST(Blake3, SingleBitChangeDifferentDigest) {
    std::vector<std::byte> v1(128, std::byte{0});
    auto                   v2 = v1;
    v2[42]                    = std::byte{0x01};
    EXPECT_NE(ftx::blake3(v1), ftx::blake3(v2));
}

}  // namespace
