#include <gtest/gtest.h>

#include <cstring>
#include <string_view>
#include <vector>

#include "ftx/util/crc32c.hpp"

namespace {

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

TEST(Crc32c, EmptyInputIsZero) {
    EXPECT_EQ(ftx::crc32c({}), 0u);
}

// Universal CRC-32C "check value": "123456789" → 0xE3069283
TEST(Crc32c, CheckString) {
    const auto v = bytes_of("123456789");
    EXPECT_EQ(ftx::crc32c(v), 0xE3069283u);
}

TEST(Crc32c, SingleAscii_a) {
    const auto v = bytes_of("a");
    EXPECT_EQ(ftx::crc32c(v), 0xC1D04330u);
}

TEST(Crc32c, ChainEqualsConcatenation) {
    const auto a   = bytes_of("hello, ");
    const auto b   = bytes_of("world");
    auto       ab  = a;
    ab.insert(ab.end(), b.begin(), b.end());

    const uint32_t direct  = ftx::crc32c(ab);
    const uint32_t chained = ftx::crc32c(b, ftx::crc32c(a));
    EXPECT_EQ(direct, chained);
}

TEST(Crc32c, BitFlipChangesCrc) {
    const auto v1 = bytes_of("payload");
    auto       v2 = v1;
    v2[3] = std::byte{static_cast<unsigned char>(std::to_integer<uint8_t>(v2[3]) ^ 0x01)};
    EXPECT_NE(ftx::crc32c(v1), ftx::crc32c(v2));
}

}  // namespace
