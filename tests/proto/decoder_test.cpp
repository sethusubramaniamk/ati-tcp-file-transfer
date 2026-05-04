#include <gtest/gtest.h>

#include <span>
#include <vector>

#include "ftx/proto/decoder.hpp"
#include "ftx/proto/frame.hpp"

namespace {

using namespace ftx::proto;  // NOLINT: anonymous namespace

std::vector<std::byte> filled(size_t n, unsigned char v) {
    return std::vector<std::byte>(n, std::byte{v});
}

TEST(FrameDecoder, SingleFrameSingleFeed) {
    const auto payload = filled(8, 0xAB);
    const auto encoded = encode_frame(FrameType::Chunk, payload);

    FrameDecoder dec;
    ASSERT_TRUE(dec.feed(encoded));
    ASSERT_TRUE(dec.has_frame());

    const auto f = dec.take_frame();
    EXPECT_EQ(f.header.type, FrameType::Chunk);
    EXPECT_EQ(f.header.payload_len, payload.size());
    EXPECT_EQ(f.payload, payload);
    EXPECT_FALSE(dec.has_frame());
    EXPECT_FALSE(dec.poisoned());
}

TEST(FrameDecoder, ZeroLengthFrame) {
    const auto encoded = encode_frame(FrameType::Ack, {});

    FrameDecoder dec;
    ASSERT_TRUE(dec.feed(encoded));
    ASSERT_TRUE(dec.has_frame());

    const auto f = dec.take_frame();
    EXPECT_EQ(f.header.type, FrameType::Ack);
    EXPECT_EQ(f.header.payload_len, 0u);
    EXPECT_TRUE(f.payload.empty());
}

TEST(FrameDecoder, MultipleFramesInOneFeed) {
    const auto p1 = filled(4, 0x11);
    const auto p2 = filled(7, 0x22);
    const auto e1 = encode_frame(FrameType::Hello, p1);
    const auto e2 = encode_frame(FrameType::Manifest, p2);

    std::vector<std::byte> combined;
    combined.insert(combined.end(), e1.begin(), e1.end());
    combined.insert(combined.end(), e2.begin(), e2.end());

    FrameDecoder dec;
    ASSERT_TRUE(dec.feed(combined));
    ASSERT_TRUE(dec.has_frame());

    const auto f1 = dec.take_frame();
    EXPECT_EQ(f1.header.type, FrameType::Hello);
    EXPECT_EQ(f1.payload, p1);

    // After taking the first frame, the decoder should auto-advance.
    ASSERT_TRUE(dec.has_frame());
    const auto f2 = dec.take_frame();
    EXPECT_EQ(f2.header.type, FrameType::Manifest);
    EXPECT_EQ(f2.payload, p2);

    EXPECT_FALSE(dec.has_frame());
}

TEST(FrameDecoder, BytewiseFeed) {
    const auto payload = filled(13, 0xCD);
    const auto encoded = encode_frame(FrameType::Chunk, payload);

    FrameDecoder dec;
    for (size_t i = 0; i + 1 < encoded.size(); ++i) {
        const auto one = std::span<const std::byte>(encoded.data() + i, 1);
        ASSERT_TRUE(dec.feed(one));
        EXPECT_FALSE(dec.has_frame()) << "frame appeared early at i=" << i;
    }
    const auto last = std::span<const std::byte>(&encoded.back(), 1);
    ASSERT_TRUE(dec.feed(last));
    ASSERT_TRUE(dec.has_frame());

    const auto f = dec.take_frame();
    EXPECT_EQ(f.payload, payload);
}

TEST(FrameDecoder, PoisonedOnBadCrc) {
    auto encoded = encode_frame(FrameType::Chunk, filled(4, 0x33));
    encoded[5] = std::byte{static_cast<unsigned char>(std::to_integer<uint8_t>(encoded[5]) ^ 0xAAu)};

    FrameDecoder dec;
    EXPECT_FALSE(dec.feed(encoded));
    EXPECT_TRUE(dec.poisoned());
    EXPECT_EQ(dec.last_error(), FrameDecoder::Error::BadCrc);

    // Subsequent feeds keep returning false.
    EXPECT_FALSE(dec.feed({}));
}

TEST(FrameDecoder, PoisonedOnOversizedPayload) {
    const auto encoded = encode_frame(FrameType::Chunk, filled(2000, 0x44));

    FrameDecoder dec(/*max_payload=*/1000);
    EXPECT_FALSE(dec.feed(encoded));
    EXPECT_TRUE(dec.poisoned());
    EXPECT_EQ(dec.last_error(), FrameDecoder::Error::PayloadTooLarge);
}

TEST(FrameDecoder, SplitOnHeaderBoundary) {
    const auto payload = filled(20, 0x77);
    const auto encoded = encode_frame(FrameType::Chunk, payload);

    FrameDecoder dec;
    // Feed first 9 bytes (just the header), then the rest.
    ASSERT_TRUE(dec.feed(std::span<const std::byte>(encoded.data(), kHeaderSize)));
    EXPECT_FALSE(dec.has_frame());
    ASSERT_TRUE(dec.feed(std::span<const std::byte>(encoded.data() + kHeaderSize,
                                                    encoded.size() - kHeaderSize)));
    ASSERT_TRUE(dec.has_frame());

    const auto f = dec.take_frame();
    EXPECT_EQ(f.payload, payload);
}

TEST(FrameDecoder, ResidualBytesPreservedAcrossFrames) {
    // Feed two complete frames glued together, then one extra incomplete frame.
    const auto p1 = filled(3, 0xA1);
    const auto p2 = filled(5, 0xB2);
    const auto e1 = encode_frame(FrameType::Hello, p1);
    const auto e2 = encode_frame(FrameType::Chunk, p2);

    std::vector<std::byte> combined;
    combined.insert(combined.end(), e1.begin(), e1.end());
    combined.insert(combined.end(), e2.begin(), e2.end());
    // Append a partial third frame: just 4 bytes.
    combined.insert(combined.end(), 4u, std::byte{0x99});

    FrameDecoder dec;
    ASSERT_TRUE(dec.feed(combined));
    ASSERT_TRUE(dec.has_frame());
    EXPECT_EQ(dec.take_frame().header.type, FrameType::Hello);
    ASSERT_TRUE(dec.has_frame());
    EXPECT_EQ(dec.take_frame().header.type, FrameType::Chunk);
    EXPECT_FALSE(dec.has_frame());
    EXPECT_FALSE(dec.poisoned());
}

}  // namespace
