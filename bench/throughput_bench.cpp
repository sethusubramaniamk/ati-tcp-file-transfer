// Quick localhost throughput probe for ftx — measures effective MiB/s for a
// single transfer. Not a microbenchmark suite; just a one-shot number for
// the perf table. Uses chrono and printf so we don't pull in a benchmark
// framework.
#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

fs::path mk_temp_dir() {
    const auto stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << "ftx-bench-" << stamp << "-" << std::this_thread::get_id();
    auto dir = fs::temp_directory_path() / oss.str();
    fs::create_directories(dir);
    return dir;
}

void write_random(const fs::path& p, uint64_t size) {
    std::mt19937                       rng(0xBEEFu);
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

double run_one(uint64_t size_bytes, uint32_t chunk_size) {
    const auto work = mk_temp_dir();
    const auto root = work / "recv";
    const auto src  = work / "src.bin";
    fs::create_directories(root);
    write_random(src, size_bytes);

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, ep, root);
    const auto             port = srv.local_port();

    std::promise<bool> done;
    std::thread        srv_thread([&]() { done.set_value(srv.run_one()); });

    asio::io_context              cli_io;
    ftx::transport::ClientOptions opts;
    opts.chunk_size = chunk_size;
    ftx::transport::Client cli(cli_io, opts);

    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = cli.send("127.0.0.1", port, src, "x.bin");
    const auto t1 = std::chrono::steady_clock::now();
    if (!ok) {
        std::fprintf(stderr, "send failed: %s\n", cli.last_error().c_str());
        srv_thread.join();
        std::exit(1);
    }
    srv_thread.join();
    (void)done.get_future().get();

    std::error_code ec;
    fs::remove_all(work, ec);

    const auto secs = std::chrono::duration<double>(t1 - t0).count();
    return (static_cast<double>(size_bytes) / (1024.0 * 1024.0)) / secs;
}

}  // namespace

int main(int argc, char** argv) {
    uint64_t size_mib = 256;
    if (argc >= 2) size_mib = std::strtoull(argv[1], nullptr, 10);

    const uint64_t size_bytes = size_mib * 1024 * 1024;
    std::printf("# ftx throughput probe — %llu MiB localhost transfer\n",
                static_cast<unsigned long long>(size_mib));
    std::printf("%-14s %s\n", "chunk_size", "throughput");
    for (uint32_t cs : {64u * 1024u, 256u * 1024u, 1024u * 1024u, 4u * 1024u * 1024u}) {
        const double mibps = run_one(size_bytes, cs);
        std::printf("%-12u  %.1f MiB/s\n", cs, mibps);
    }
    return 0;
}
