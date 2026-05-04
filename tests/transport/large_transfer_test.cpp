// Heavy integration tests gated by the FTX_LARGE_TEST environment variable.
// Set FTX_LARGE_TEST=1 to enable the 1 GiB transfer.
#include <gtest/gtest.h>

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "ftx/transport/client.hpp"
#include "ftx/transport/server.hpp"

namespace {

namespace fs = std::filesystem;

bool large_test_enabled() {
    const char* env = std::getenv("FTX_LARGE_TEST");
    return env != nullptr && std::strlen(env) > 0 && env[0] != '0';
}

fs::path mk_temp_dir(const std::string& tag) {
    const auto stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << "ftx-large-" << tag << "-" << stamp << "-" << std::this_thread::get_id();
    auto dir = fs::temp_directory_path() / oss.str();
    fs::create_directories(dir);
    return dir;
}

void write_pseudo_random_file(const fs::path& p, uint64_t size, uint32_t seed) {
    std::mt19937                       rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::ofstream                      f(p, std::ios::binary);
    constexpr size_t                   kBlock = 1024 * 1024;
    std::vector<char>                  buf(kBlock);
    uint64_t                           remaining = size;
    while (remaining > 0) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlock, remaining));
        for (size_t i = 0; i < n; ++i) buf[i] = static_cast<char>(dist(rng));
        f.write(buf.data(), static_cast<std::streamsize>(n));
        remaining -= n;
    }
}

bool files_equal(const fs::path& a, const fs::path& b) {
    if (fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream    fa(a, std::ios::binary);
    std::ifstream    fb(b, std::ios::binary);
    constexpr size_t kBlock = 1024 * 1024;
    std::vector<char> ba(kBlock), bb(kBlock);
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

TEST(LargeTransfer, OneGiBRoundtrip) {
    if (!large_test_enabled()) {
        GTEST_SKIP() << "set FTX_LARGE_TEST=1 to enable 1 GiB transfer";
    }

    constexpr uint64_t kSize = 1ull * 1024 * 1024 * 1024;
    const auto         work  = mk_temp_dir("1gib");
    const auto         root  = work / "recv";
    const auto         src   = work / "src.bin";
    fs::create_directories(root);

    write_pseudo_random_file(src, kSize, /*seed=*/0xDEADu);

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, ep, root);
    const auto             port = srv.local_port();

    std::promise<bool> done;
    std::thread        srv_thread([&]() { done.set_value(srv.run_one()); });

    asio::io_context              cli_io;
    ftx::transport::ClientOptions opts;
    opts.chunk_size = 4 * 1024 * 1024;  // 4 MiB chunks for 1 GiB
    ftx::transport::Client cli(cli_io, opts);

    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = cli.send("127.0.0.1", port, src, "big.bin");
    const auto t1 = std::chrono::steady_clock::now();
    EXPECT_TRUE(ok) << cli.last_error();
    srv_thread.join();
    EXPECT_TRUE(done.get_future().get());

    const auto secs = std::chrono::duration<double>(t1 - t0).count();
    const auto mibps = (static_cast<double>(kSize) / (1024.0 * 1024.0)) / secs;
    std::printf("\n[ftx] 1 GiB transfer: %.2f s (%.1f MiB/s)\n", secs, mibps);

    EXPECT_TRUE(files_equal(src, root / "big.bin"));

    std::error_code ec;
    fs::remove_all(work, ec);
}

}  // namespace
