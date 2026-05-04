#include "ftx/util/crc32c.hpp"

#include <array>

namespace ftx {

namespace {

constexpr uint32_t kPolyReflected = 0x82F63B78u;  // CRC-32C, reflected

constexpr auto build_table() noexcept {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = ((c & 1u) != 0u) ? (c >> 1) ^ kPolyReflected : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

constexpr auto kTable = build_table();

}  // namespace

uint32_t crc32c(std::span<const std::byte> data, uint32_t seed) noexcept {
    uint32_t c = ~seed;
    for (auto b : data) {
        const uint8_t byte = std::to_integer<uint8_t>(b);
        c = (c >> 8) ^ kTable[(c ^ byte) & 0xFFu];
    }
    return ~c;
}

}  // namespace ftx
