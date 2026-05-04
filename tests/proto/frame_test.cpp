#include "ftx/proto/frame.hpp"

#include <array>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "ftx/util/crc32c.hpp"

namespace {

using namespace ftx::proto;  // NOLINT: scoped to anonymous namespace

TEST(FrameHeader, EncodeDecodeRoundtrip) {
    const FrameHeader h{.payload_len = 12345, .type = FrameType::Manifest};
    std::array<std::byte, kHeaderSize> buf{};
    h.encode_to(buf);

    const auto decoded = FrameHeader::decode_from(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->payload_len, 12345u);
    EXPECT_EQ(decoded->type, FrameType::Manifest);
}

TEST(FrameHeader, ZeroLengthPayloadRoundtrip) {
    const FrameHeader h{.payload_len = 0, .type = FrameType::Ack};
    std::array<std::byte, kHeaderSize> buf{};
    h.encode_to(buf);

    const auto decoded = FrameHeader::decode_from(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->payload_len, 0u);
    EXPECT_EQ(decoded->type, FrameType::Ack);
}

TEST(FrameHeader, AllKnownTypesRoundtrip) {
    constexpr std::array<FrameType, 7> all{
        FrameType::Hello,
        FrameType::Manifest,
        FrameType::ReqChunks,
        FrameType::Chunk,
        FrameType::Ack,
        FrameType::Complete,
        FrameType::Error,
    };
    for (const auto t : all) {
        const FrameHeader h{.payload_len = 42, .type = t};
        std::array<std::byte, kHeaderSize> buf{};
        h.encode_to(buf);
        const auto d = FrameHeader::decode_from(buf);
        ASSERT_TRUE(d.has_value()) << "type=" << static_cast<int>(t);
        EXPECT_EQ(d->type, t);
    }
}

TEST(FrameHeader, RejectsBadCrc) {
    const FrameHeader h{.payload_len = 100, .type = FrameType::Chunk};
    std::array<std::byte, kHeaderSize> buf{};
    h.encode_to(buf);
    // Flip a CRC byte.
    buf[5] = std::byte{static_cast<unsigned char>(std::to_integer<uint8_t>(buf[5]) ^ 0xFF)};

    FrameHeader::DecodeError err{};
    const auto decoded = FrameHeader::decode_from(buf, kDefaultMaxPayload, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(err, FrameHeader::DecodeError::BadCrc);
}

TEST(FrameHeader, RejectsUnknownTypeWithValidCrc) {
    std::array<std::byte, kHeaderSize> buf{};
    // payload_len = 0 (already default).
    buf[4] = std::byte{0x55};  // unassigned type
    const auto covered = std::span<const std::byte>(buf.data(), 5);
    const uint32_t crc = ftx::crc32c(covered);
    buf[5] = std::byte{static_cast<unsigned char>((crc >> 24) & 0xFFu)};
    buf[6] = std::byte{static_cast<unsigned char>((crc >> 16) & 0xFFu)};
    buf[7] = std::byte{static_cast<unsigned char>((crc >> 8) & 0xFFu)};
    buf[8] = std::byte{static_cast<unsigned char>(crc & 0xFFu)};

    FrameHeader::DecodeError err{};
    const auto decoded = FrameHeader::decode_from(buf, kDefaultMaxPayload, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(err, FrameHeader::DecodeError::UnknownType);
}

TEST(FrameHeader, RejectsOversizedPayload) {
    const FrameHeader h{.payload_len = 1000, .type = FrameType::Chunk};
    std::array<std::byte, kHeaderSize> buf{};
    h.encode_to(buf);

    FrameHeader::DecodeError err{};
    const auto decoded = FrameHeader::decode_from(buf, /*max_payload=*/500, &err);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(err, FrameHeader::DecodeError::PayloadTooLarge);
}

TEST(FrameHeader, BoundaryPayloadAllowed) {
    const FrameHeader h{.payload_len = 500, .type = FrameType::Chunk};
    std::array<std::byte, kHeaderSize> buf{};
    h.encode_to(buf);

    const auto decoded = FrameHeader::decode_from(buf, /*max_payload=*/500);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->payload_len, 500u);
}

TEST(EncodeFrame, ProducesHeaderPlusPayload) {
    const std::array<unsigned char, 4> raw{0xDE, 0xAD, 0xBE, 0xEF};
    const auto payload = std::as_bytes(std::span(raw));

    const auto encoded = encode_frame(FrameType::Hello, payload);
    EXPECT_EQ(encoded.size(), kHeaderSize + raw.size());

    const auto hdr = FrameHeader::decode_from(
        std::span<const std::byte, kHeaderSize>(encoded.data(), kHeaderSize));
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->type, FrameType::Hello);
    EXPECT_EQ(hdr->payload_len, raw.size());

    EXPECT_EQ(std::to_integer<uint8_t>(encoded[kHeaderSize + 0]), 0xDEu);
    EXPECT_EQ(std::to_integer<uint8_t>(encoded[kHeaderSize + 1]), 0xADu);
    EXPECT_EQ(std::to_integer<uint8_t>(encoded[kHeaderSize + 2]), 0xBEu);
    EXPECT_EQ(std::to_integer<uint8_t>(encoded[kHeaderSize + 3]), 0xEFu);
}

TEST(EncodeFrame, EmptyPayload) {
    const auto encoded = encode_frame(FrameType::Ack, {});
    EXPECT_EQ(encoded.size(), kHeaderSize);
    const auto hdr = FrameHeader::decode_from(
        std::span<const std::byte, kHeaderSize>(encoded.data(), kHeaderSize));
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->payload_len, 0u);
    EXPECT_EQ(hdr->type, FrameType::Ack);
}

TEST(EncodeFrame, ThrowsOnOversizedPayload) {
    // Allocate-on-stack-impossible; just construct a span pointing to a
    // pretend-large buffer using a small real one and a faked size.
    // Cleaner: actually allocate an oversized payload? That'd waste memory in
    // tests. Use the documented contract: encode_frame throws on >max.
    std::vector<std::byte> too_big(kDefaultMaxPayload + 1, std::byte{0});
    EXPECT_THROW({ (void)encode_frame(FrameType::Chunk, too_big); }, std::invalid_argument);
}

}  // namespace
