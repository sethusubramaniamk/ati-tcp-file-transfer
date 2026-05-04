#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "ftx/io/resume_state.hpp"

namespace {

namespace fs = std::filesystem;

fs::path tmpfile(const std::string& tag) {
    const auto stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << "ftx-rs-" << tag << "-" << stamp << "-" << std::this_thread::get_id() << ".bin";
    return fs::temp_directory_path() / oss.str();
}

ftx::io::ResumeState::ManifestId fake_id(unsigned char fill) {
    ftx::io::ResumeState::ManifestId id{};
    for (auto& b : id) b = std::byte{fill};
    return id;
}

TEST(ResumeState, FreshStateAllMissing) {
    ftx::io::ResumeState s(fake_id(0xAB), 17);
    EXPECT_EQ(s.chunk_count(), 17u);
    EXPECT_EQ(s.missing().size(), 17u);
    EXPECT_FALSE(s.complete());
    for (uint32_t i = 0; i < 17; ++i) EXPECT_FALSE(s.is_set(i));
}

TEST(ResumeState, MarkAndQuery) {
    ftx::io::ResumeState s(fake_id(0x01), 100);
    s.mark_received(0);
    s.mark_received(7);
    s.mark_received(99);
    EXPECT_TRUE(s.is_set(0));
    EXPECT_TRUE(s.is_set(7));
    EXPECT_TRUE(s.is_set(99));
    EXPECT_FALSE(s.is_set(1));
    EXPECT_FALSE(s.is_set(98));
    EXPECT_EQ(s.missing().size(), 97u);
    EXPECT_FALSE(s.complete());
}

TEST(ResumeState, MarkAllIsComplete) {
    ftx::io::ResumeState s(fake_id(0xFF), 9);
    for (uint32_t i = 0; i < 9; ++i) s.mark_received(i);
    EXPECT_TRUE(s.complete());
    EXPECT_TRUE(s.missing().empty());
}

TEST(ResumeState, RoundtripsToDisk) {
    const auto path = tmpfile("roundtrip");
    ftx::io::ResumeState in(fake_id(0x42), 65);
    in.mark_received(0);
    in.mark_received(33);
    in.mark_received(64);
    ASSERT_TRUE(in.save(path));

    ftx::io::ResumeState out;
    ASSERT_TRUE(ftx::io::ResumeState::load(path, out));

    EXPECT_EQ(out.manifest_id(), in.manifest_id());
    EXPECT_EQ(out.chunk_count(), in.chunk_count());
    EXPECT_TRUE(out.is_set(0));
    EXPECT_TRUE(out.is_set(33));
    EXPECT_TRUE(out.is_set(64));
    EXPECT_FALSE(out.is_set(1));
    EXPECT_FALSE(out.is_set(63));

    ftx::io::ResumeState::remove(path);
    EXPECT_FALSE(fs::exists(path));
}

TEST(ResumeState, LoadMissingFileReturnsFalse) {
    const auto           path = tmpfile("missing");
    ftx::io::ResumeState s;
    EXPECT_FALSE(ftx::io::ResumeState::load(path, s));
}

TEST(ResumeState, LoadCorruptHeaderReturnsFalse) {
    const auto path = tmpfile("corrupt");
    {
        std::ofstream f(path, std::ios::binary);
        f << "NOPE";
    }
    ftx::io::ResumeState s;
    EXPECT_FALSE(ftx::io::ResumeState::load(path, s));
    fs::remove(path);
}

}  // namespace
