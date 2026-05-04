#include <gtest/gtest.h>

#include <cstring>

#include "ftx/proto/messages.hpp"

namespace {

using namespace ftx::proto;  // NOLINT: anonymous namespace

TEST(HelloMsg, Roundtrip) {
    const HelloMsg in{.protocol_version = 1, .max_chunk_size = 4096, .capabilities = 0xCAFEu};
    const auto     enc = encode_hello(in);
    EXPECT_EQ(enc.size(), 1u + 4u + 4u);
    const auto out = decode_hello(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->protocol_version, in.protocol_version);
    EXPECT_EQ(out->max_chunk_size,   in.max_chunk_size);
    EXPECT_EQ(out->capabilities,     in.capabilities);
}

TEST(HelloMsg, RejectsTruncated) {
    EXPECT_FALSE(decode_hello({}).has_value());
    const auto enc = encode_hello(HelloMsg{});
    EXPECT_FALSE(decode_hello(std::span<const std::byte>(enc.data(), enc.size() - 1)).has_value());
}

TEST(HelloMsg, RejectsTrailingGarbage) {
    auto enc = encode_hello(HelloMsg{});
    enc.push_back(std::byte{0});
    EXPECT_FALSE(decode_hello(enc).has_value());
}

TEST(ManifestMsg, RoundtripEmptyHashes) {
    ManifestMsg in{};
    in.file_size   = 1024 * 1024;
    in.chunk_size  = 1024 * 1024;
    in.chunk_count = 1;
    in.path        = "subdir/file.bin";
    in.chunk_hashes.resize(1);

    const auto enc = encode_manifest(in);
    const auto out = decode_manifest(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->file_size,   in.file_size);
    EXPECT_EQ(out->chunk_size,  in.chunk_size);
    EXPECT_EQ(out->chunk_count, in.chunk_count);
    EXPECT_EQ(out->path,        in.path);
    EXPECT_EQ(out->chunk_hashes.size(), 1u);
    EXPECT_EQ(out->root_hash, kZeroHash);
}

TEST(ManifestMsg, RoundtripWithMultipleChunkHashes) {
    ManifestMsg in{};
    in.file_size   = 5 * 1024 * 1024;
    in.chunk_size  = 1024 * 1024;
    in.chunk_count = 5;
    in.path        = "x.bin";
    in.chunk_hashes.resize(5);
    for (size_t i = 0; i < 5; ++i) {
        for (size_t j = 0; j < in.chunk_hashes[i].size(); ++j) {
            in.chunk_hashes[i][j] = std::byte{static_cast<unsigned char>(i * 32u + j)};
        }
    }
    for (auto& b : in.root_hash) b = std::byte{0xAA};

    const auto enc = encode_manifest(in);
    const auto out = decode_manifest(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->root_hash, in.root_hash);
    EXPECT_EQ(out->chunk_hashes, in.chunk_hashes);
}

TEST(ChunkMsg, Roundtrip) {
    ChunkMsg in{};
    in.index = 42;
    in.data  = std::vector<std::byte>(128, std::byte{0x5A});
    for (auto& b : in.hash) b = std::byte{0x10};

    const auto enc = encode_chunk(in);
    EXPECT_EQ(enc.size(), 4u + 32u + in.data.size());

    const auto out = decode_chunk(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->index, in.index);
    EXPECT_EQ(out->hash,  in.hash);
    EXPECT_EQ(out->data,  in.data);
}

TEST(ChunkMsg, ZeroLengthData) {
    ChunkMsg in{};
    in.index = 7;
    const auto enc = encode_chunk(in);
    const auto out = decode_chunk(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->index, 7u);
    EXPECT_TRUE(out->data.empty());
}

TEST(CompleteMsg, Roundtrip) {
    CompleteMsg in{};
    for (auto& b : in.final_root_hash) b = std::byte{0xCC};
    in.status = 0;
    const auto enc = encode_complete(in);
    EXPECT_EQ(enc.size(), 32u + 1u);
    const auto out = decode_complete(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->status, 0u);
    EXPECT_EQ(out->final_root_hash, in.final_root_hash);
}

TEST(AckMsg, Roundtrip) {
    const AckMsg in{.last_index = 0xDEADBEEFu};
    const auto   enc = encode_ack(in);
    EXPECT_EQ(enc.size(), 4u);
    const auto out = decode_ack(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->last_index, in.last_index);
}

TEST(ErrorMsg, RoundtripWithMessage) {
    const ErrorMsg in{.code = ErrorCode::InvalidPath, .message = "rejected: ../etc/passwd"};
    const auto     enc = encode_error(in);
    const auto     out = decode_error(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->code,    in.code);
    EXPECT_EQ(out->message, in.message);
}

TEST(ErrorMsg, RoundtripEmptyMessage) {
    const ErrorMsg in{.code = ErrorCode::Unspecified, .message = ""};
    const auto     enc = encode_error(in);
    const auto     out = decode_error(enc);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->code,    in.code);
    EXPECT_EQ(out->message, in.message);
}

}  // namespace
