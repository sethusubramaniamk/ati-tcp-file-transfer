#include <gtest/gtest.h>

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
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
#include "ftx/transport/tls.hpp"

#ifndef FTX_TEST_CERTS_DIR
#error "FTX_TEST_CERTS_DIR must be defined by the build system"
#endif

namespace {

namespace fs = std::filesystem;

fs::path certs_dir() { return fs::path(FTX_TEST_CERTS_DIR); }

fs::path mk_temp(const std::string& tag) {
    const auto stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << "ftx-tls-" << tag << "-" << stamp << "-" << std::this_thread::get_id();
    auto p = fs::temp_directory_path() / oss.str();
    fs::create_directories(p);
    return p;
}

void write_random_file(const fs::path& p, uint64_t size, uint32_t seed) {
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

ftx::transport::TlsConfig server_tls(bool verify_peer = true) {
    ftx::transport::TlsConfig c;
    c.cert_path   = (certs_dir() / "server.crt").string();
    c.key_path    = (certs_dir() / "server.key").string();
    c.ca_path     = (certs_dir() / "ca.crt").string();
    c.verify_peer = verify_peer;
    return c;
}

ftx::transport::TlsConfig client_tls() {
    ftx::transport::TlsConfig c;
    c.cert_path   = (certs_dir() / "client.crt").string();
    c.key_path    = (certs_dir() / "client.key").string();
    c.ca_path     = (certs_dir() / "ca.crt").string();
    c.verify_peer = true;
    c.sni_host    = "localhost";
    return c;
}

class TlsLoopback : public ::testing::Test {
 protected:
    void SetUp() override {
        work_ = mk_temp("loopback");
        root_ = work_ / "recv";
        src_  = work_ / "src.bin";
        fs::create_directories(root_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(work_, ec);
    }

    fs::path work_, root_, src_;
};

TEST_F(TlsLoopback, OneMegRoundtripsOverMtls) {
    write_random_file(src_, 1024 * 1024 + 333, /*seed=*/0xBEEFu);

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, ep, root_, server_tls(/*verify_peer=*/true));
    const auto             port = srv.local_port();

    std::promise<bool> done;
    std::thread        srv_thread([&]() { done.set_value(srv.run_one()); });

    asio::io_context        cli_io;
    ftx::transport::ClientOptions opts;
    opts.chunk_size = 256 * 1024;
    opts.tls        = client_tls();
    ftx::transport::Client cli(cli_io, opts);

    const std::string remote = "tls.bin";
    const bool        ok     = cli.send("127.0.0.1", port, src_, remote);
    EXPECT_TRUE(ok) << cli.last_error();

    srv_thread.join();
    EXPECT_TRUE(done.get_future().get());
    EXPECT_TRUE(files_equal(src_, root_ / remote));
}

TEST_F(TlsLoopback, ServerRejectsClientWithNoCert) {
    write_random_file(src_, 4096, /*seed=*/1);

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, ep, root_, server_tls(/*verify_peer=*/true));
    const auto             port = srv.local_port();

    std::thread srv_thread([&]() { (void)srv.run_one(); });

    asio::io_context cli_io;
    ftx::transport::ClientOptions opts;
    auto bad = client_tls();
    bad.cert_path.clear();  // no cert presented
    bad.key_path.clear();
    opts.tls = bad;
    ftx::transport::Client cli(cli_io, opts);

    const bool ok = cli.send("127.0.0.1", port, src_, "x.bin");
    EXPECT_FALSE(ok);
    srv_thread.join();
}

TEST_F(TlsLoopback, ClientRejectsUntrustedServer) {
    write_random_file(src_, 4096, /*seed=*/2);

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, ep, root_, server_tls(/*verify_peer=*/false));
    const auto             port = srv.local_port();

    std::thread srv_thread([&]() { (void)srv.run_one(); });

    asio::io_context cli_io;
    ftx::transport::ClientOptions opts;
    auto bad = client_tls();
    bad.ca_path.clear();  // client has no trust anchors → server's cert is untrusted
    opts.tls = bad;
    ftx::transport::Client cli(cli_io, opts);

    const bool ok = cli.send("127.0.0.1", port, src_, "y.bin");
    EXPECT_FALSE(ok);
    srv_thread.join();
}

}  // namespace
