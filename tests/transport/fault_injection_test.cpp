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

#include <asio.hpp>
#include <gtest/gtest.h>

#include "ftx/io/file_source.hpp"
#include "ftx/io/resume_state.hpp"
#include "ftx/proto/frame.hpp"
#include "ftx/proto/messages.hpp"
#include "ftx/transport/client.hpp"
#include "ftx/transport/connection.hpp"
#include "ftx/transport/server.hpp"
#include "ftx/util/blake3.hpp"

namespace {

namespace fs = std::filesystem;

fs::path mk_temp(const std::string& tag) {
    const auto stamp =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << "ftx-fi-" << tag << "-" << stamp << "-" << std::this_thread::get_id();
    auto p = fs::temp_directory_path() / oss.str();
    fs::create_directories(p);
    return p;
}

void write_pseudo_random_file(const fs::path& p, uint64_t size, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::ofstream f(p, std::ios::binary);
    constexpr size_t kBlock = 64 * 1024;
    std::vector<char> buf(kBlock);
    uint64_t remaining = size;
    while (remaining > 0) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(kBlock, remaining));
        for (size_t i = 0; i < n; ++i)
            buf[i] = static_cast<char>(dist(rng));
        f.write(buf.data(), static_cast<std::streamsize>(n));
        remaining -= n;
    }
}

bool files_equal(const fs::path& a, const fs::path& b) {
    if (fs::file_size(a) != fs::file_size(b))
        return false;
    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    constexpr size_t kBlock = 64 * 1024;
    std::vector<char> ba(kBlock), bb(kBlock);
    while (fa.good() && fb.good()) {
        fa.read(ba.data(), static_cast<std::streamsize>(kBlock));
        fb.read(bb.data(), static_cast<std::streamsize>(kBlock));
        const auto na = fa.gcount();
        const auto nb = fb.gcount();
        if (na != nb)
            return false;
        if (na == 0)
            return true;
        if (std::memcmp(ba.data(), bb.data(), static_cast<size_t>(na)) != 0)
            return false;
    }
    return true;
}

// Replicate the client's manifest construction so the test can pre-seed a
// matching .ftxstate file before any transfer runs.
ftx::proto::ManifestMsg build_manifest(const fs::path& src,
                                       const std::string& remote_dest,
                                       uint32_t chunk_size) {
    ftx::io::FileSource s(src);
    if (!s.open()) {
        ADD_FAILURE() << "build_manifest: open: " << s.last_error();
    }
    const uint64_t size = s.size();
    const uint32_t cc =
        (size == 0) ? 0 : static_cast<uint32_t>((size + chunk_size - 1) / chunk_size);

    ftx::proto::ManifestMsg m;
    m.file_size = size;
    m.chunk_size = chunk_size;
    m.chunk_count = cc;
    m.path = remote_dest;
    m.chunk_hashes.resize(cc);

    ftx::Blake3Hasher rh;
    std::vector<std::byte> buf(chunk_size);
    for (uint32_t i = 0; i < cc; ++i) {
        size_t n = 0;
        if (!s.read_at(static_cast<uint64_t>(i) * chunk_size, buf, &n)) {
            ADD_FAILURE() << "read_at: " << s.last_error();
        }
        const auto view = std::span<const std::byte>(buf.data(), n);
        m.chunk_hashes[i] = ftx::blake3(view);
        rh.update(view);
    }
    m.root_hash = rh.finalize();
    return m;
}

// ---------------------------------------------------------------------------
// 1. Resume "no work needed" — pre-populate a complete .partial and a
//    .ftxstate with all chunks marked received. The transfer should produce
//    the correct file with zero chunks actually transmitted.
// ---------------------------------------------------------------------------
TEST(FaultInjection, ResumeWithFullyPopulatedStateSendsNoChunks) {
    const auto work = mk_temp("resume-noop");
    const auto root = work / "recv";
    const auto src = work / "src.bin";
    fs::create_directories(root);

    constexpr uint32_t kChunk = 64 * 1024;
    constexpr uint64_t kSize = 700 * 1024;  // ~11 chunks
    write_pseudo_random_file(src, kSize, /*seed=*/0xACE1u);

    const std::string remote_dest = "resume.bin";
    const auto manifest = build_manifest(src, remote_dest, kChunk);
    const auto manifest_id = ftx::blake3(ftx::proto::encode_manifest(manifest));

    // Pre-seed: copy the source into root/<remote>.partial, then mark every
    // chunk as already received in the .ftxstate sidecar.
    const auto partial_path = root / (remote_dest + ".partial");
    const auto state_path = root / (remote_dest + ".ftxstate");
    fs::copy_file(src, partial_path);

    ftx::io::ResumeState state(manifest_id, manifest.chunk_count);
    for (uint32_t i = 0; i < manifest.chunk_count; ++i)
        state.mark_received(i);
    ASSERT_TRUE(state.save(state_path));

    // Run the transfer.
    asio::io_context srv_io;
    const asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, ep, root);
    const auto port = srv.local_port();

    std::promise<bool> done;
    std::thread srv_thread([&]() { done.set_value(srv.run_one()); });

    asio::io_context cli_io;
    ftx::transport::ClientOptions opts;
    opts.chunk_size = kChunk;
    ftx::transport::Client cli(cli_io, opts);
    EXPECT_TRUE(cli.send("127.0.0.1", port, src, remote_dest)) << cli.last_error();

    srv_thread.join();
    EXPECT_TRUE(done.get_future().get());

    // Final file must be present + correct; .ftxstate cleaned up.
    EXPECT_TRUE(fs::exists(root / remote_dest));
    EXPECT_FALSE(fs::exists(state_path));
    EXPECT_FALSE(fs::exists(partial_path));
    EXPECT_TRUE(files_equal(src, root / remote_dest));

    std::error_code ec;
    fs::remove_all(work, ec);
}

// ---------------------------------------------------------------------------
// 2. Stale .ftxstate (different manifest) — server must wipe it and start
//    fresh, succeeding via the normal full-transfer path.
// ---------------------------------------------------------------------------
TEST(FaultInjection, StaleStateForcesFreshTransfer) {
    const auto work = mk_temp("stale-state");
    const auto root = work / "recv";
    const auto src = work / "src.bin";
    fs::create_directories(root);
    write_pseudo_random_file(src, 12345, /*seed=*/0x42u);

    // Plant a state file with a non-matching manifest_id.
    const std::string remote_dest = "stale.bin";
    const auto state_path = root / (remote_dest + ".ftxstate");
    ftx::io::ResumeState::ManifestId bogus{};
    for (auto& b : bogus)
        b = std::byte{0xAB};
    ftx::io::ResumeState bogus_state(bogus, 999);
    for (uint32_t i = 0; i < 50; ++i)
        bogus_state.mark_received(i);
    ASSERT_TRUE(bogus_state.save(state_path));

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, ep, root);
    const auto port = srv.local_port();

    std::thread srv_thread([&]() { (void)srv.run_one(); });

    asio::io_context cli_io;
    ftx::transport::Client cli(cli_io);
    EXPECT_TRUE(cli.send("127.0.0.1", port, src, remote_dest)) << cli.last_error();
    srv_thread.join();

    EXPECT_TRUE(files_equal(src, root / remote_dest));
    EXPECT_FALSE(fs::exists(state_path));

    std::error_code ec;
    fs::remove_all(work, ec);
}

// ---------------------------------------------------------------------------
// 3. Tampered chunk — adversarial client sends a chunk whose payload doesn't
//    match its declared BLAKE3 hash. Server must reject with HashMismatch.
// ---------------------------------------------------------------------------
TEST(FaultInjection, ServerRejectsChunkWithWrongHash) {
    const auto work = mk_temp("tamper");
    const auto root = work / "recv";
    fs::create_directories(root);

    asio::io_context srv_io;
    const asio::ip::tcp::endpoint ep(asio::ip::address_v4::loopback(), 0);
    ftx::transport::Server srv(srv_io, ep, root);
    const auto port = srv.local_port();

    std::thread srv_thread([&]() { (void)srv.run_one(); });

    // Hand-roll the adversarial client over a raw socket.
    asio::io_context cli_io;
    asio::ip::tcp::resolver resolver(cli_io);
    asio::error_code ec;
    const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port), ec);
    ASSERT_FALSE(ec);
    asio::ip::tcp::socket sock(cli_io);
    asio::connect(sock, endpoints, ec);
    ASSERT_FALSE(ec);

    ftx::transport::Connection conn(std::move(sock));

    // HELLO.
    ASSERT_TRUE(conn.send_frame(ftx::proto::FrameType::Hello,
                                ftx::proto::encode_hello(ftx::proto::HelloMsg{})));
    auto hello = conn.recv_frame();
    ASSERT_TRUE(hello.has_value());
    ASSERT_EQ(hello->header.type, ftx::proto::FrameType::Hello);

    // MANIFEST: declare 1 chunk of 100 bytes; chunk_hash is computed from
    // the *legitimate* payload but we'll send tampered bytes.
    constexpr uint32_t kChunkSize = 100;
    std::vector<std::byte> honest(kChunkSize, std::byte{0x42});
    const auto honest_hash = ftx::blake3(honest);

    ftx::proto::ManifestMsg manifest;
    manifest.file_size = kChunkSize;
    manifest.chunk_size = kChunkSize;
    manifest.chunk_count = 1;
    manifest.path = "tampered.bin";
    manifest.chunk_hashes = {honest_hash};
    manifest.root_hash = ftx::blake3(honest);

    ASSERT_TRUE(
        conn.send_frame(ftx::proto::FrameType::Manifest, ftx::proto::encode_manifest(manifest)));

    // REQ_CHUNKS — should ask for index 0.
    auto req = conn.recv_frame();
    ASSERT_TRUE(req.has_value());
    ASSERT_EQ(req->header.type, ftx::proto::FrameType::ReqChunks);

    // Send tampered CHUNK: hash claims honest_hash but data is different.
    ftx::proto::ChunkMsg chunk;
    chunk.index = 0;
    chunk.hash = honest_hash;
    chunk.data = std::vector<std::byte>(kChunkSize, std::byte{0x99});  // mismatched!
    ASSERT_TRUE(conn.send_frame(ftx::proto::FrameType::Chunk, ftx::proto::encode_chunk(chunk)));

    // Expect ERROR with HashMismatch.
    auto resp = conn.recv_frame();
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->header.type, ftx::proto::FrameType::Error);
    const auto err = ftx::proto::decode_error(resp->payload);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code, ftx::proto::ErrorCode::HashMismatch);

    srv_thread.join();
    EXPECT_FALSE(fs::exists(root / "tampered.bin"));

    std::error_code rec;
    fs::remove_all(work, rec);
}

}  // namespace
