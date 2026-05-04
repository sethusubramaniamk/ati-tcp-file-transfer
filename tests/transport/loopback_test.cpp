#include <gtest/gtest.h>

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ftx/transport/client.hpp"
#include "ftx/transport/server.hpp"

namespace {

namespace fs = std::filesystem;

fs::path make_unique_temp_dir(const std::string& tag) {
    const auto base  = fs::temp_directory_path();
    const auto stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << "ftx-" << tag << "-" << stamp << "-" << std::this_thread::get_id();
    auto dir = base / oss.str();
    fs::create_directories(dir);
    return dir;
}

void write_pseudo_random_file(const fs::path& p, uint64_t size, uint32_t seed) {
    std::mt19937                     rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::ofstream                    f(p, std::ios::binary);
    constexpr size_t                 kBlock = 64 * 1024;
    std::vector<char>                buf(kBlock);
    uint64_t                         remaining = size;
    while (remaining > 0) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlock, remaining));
        for (size_t i = 0; i < n; ++i) buf[i] = static_cast<char>(dist(rng));
        f.write(buf.data(), static_cast<std::streamsize>(n));
        remaining -= n;
    }
}

bool files_equal(const fs::path& a, const fs::path& b) {
    if (fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream                fa(a, std::ios::binary);
    std::ifstream                fb(b, std::ios::binary);
    constexpr size_t             kBlock = 64 * 1024;
    std::vector<char>            ba(kBlock), bb(kBlock);
    while (fa.good() && fb.good()) {
        fa.read(ba.data(), static_cast<std::streamsize>(kBlock));
        fb.read(bb.data(), static_cast<std::streamsize>(kBlock));
        const auto na = fa.gcount();
        const auto nb = fb.gcount();
        if (na != nb) return false;
        if (na == 0)  return true;
        if (std::memcmp(ba.data(), bb.data(), static_cast<size_t>(na)) != 0) return false;
    }
    return true;
}

class Loopback : public ::testing::TestWithParam<uint64_t> {
 protected:
    void SetUp() override {
        work_dir_ = make_unique_temp_dir("loopback");
        root_dir_ = work_dir_ / "recv-root";
        src_path_ = work_dir_ / "src.bin";
        fs::create_directories(root_dir_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(work_dir_, ec);
    }

    fs::path work_dir_;
    fs::path root_dir_;
    fs::path src_path_;
};

TEST_P(Loopback, SingleFileTransferRoundtrips) {
    const uint64_t size = GetParam();
    write_pseudo_random_file(src_path_, size, /*seed=*/0xC0FFEEu);

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint endpoint(
        asio::ip::address_v4::loopback(), 0);  // OS-assigned port

    ftx::transport::Server srv(srv_io, endpoint, root_dir_);
    const auto             port = srv.local_port();

    std::promise<bool> server_done;
    std::thread        srv_thread([&]() { server_done.set_value(srv.run_one()); });

    asio::io_context cli_io;
    ftx::transport::Client::Options opts;
    opts.chunk_size = 256 * 1024;  // small chunks to exercise multi-chunk path
    ftx::transport::Client cli(cli_io, opts);

    const std::string remote_name = "received.bin";
    const bool        ok          = cli.send("127.0.0.1", port, src_path_, remote_name);
    EXPECT_TRUE(ok) << cli.last_error();

    srv_thread.join();
    EXPECT_TRUE(server_done.get_future().get());

    const auto recv_path = root_dir_ / remote_name;
    ASSERT_TRUE(fs::exists(recv_path));
    EXPECT_EQ(fs::file_size(recv_path), size);
    EXPECT_TRUE(files_equal(src_path_, recv_path));
}

INSTANTIATE_TEST_SUITE_P(Sizes, Loopback,
                         ::testing::Values<uint64_t>(0,           // empty file
                                                     1,           // single byte
                                                     1024,        // < one chunk
                                                     256 * 1024,  // exactly one chunk
                                                     257 * 1024,  // two chunks, last short
                                                     1024 * 1024,
                                                     1024 * 1024 + 7));

TEST(Loopback, RejectsPathTraversal) {
    const auto       work = make_unique_temp_dir("traversal");
    const auto       root = work / "root";
    const auto       src  = work / "src.bin";
    fs::create_directories(root);
    write_pseudo_random_file(src, 32, /*seed=*/1);

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint endpoint(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, endpoint, root);
    const auto             port = srv.local_port();

    std::thread srv_thread([&]() { (void)srv.run_one(); });

    asio::io_context        cli_io;
    ftx::transport::Client  cli(cli_io);
    const bool              ok = cli.send("127.0.0.1", port, src, "../escape.bin");
    EXPECT_FALSE(ok);

    srv_thread.join();

    EXPECT_FALSE(fs::exists(work / "escape.bin"));
    EXPECT_FALSE(fs::exists(root / "escape.bin"));

    std::error_code ec;
    fs::remove_all(work, ec);
}

}  // namespace
