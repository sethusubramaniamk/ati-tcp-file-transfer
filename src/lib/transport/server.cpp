#include "ftx/transport/server.hpp"

#include <fstream>
#include <system_error>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "ftx/io/file_sink.hpp"
#include "ftx/io/resume_state.hpp"
#include "ftx/proto/messages.hpp"
#include "ftx/transport/connection.hpp"
#include "ftx/util/blake3.hpp"

namespace ftx::transport {

namespace {

// Reject paths that are absolute, contain ".." segments, or end up outside
// the configured root directory.
bool is_path_safe(const std::string& rel) {
    if (rel.empty())
        return false;
    const std::filesystem::path p(rel);
    if (p.is_absolute())
        return false;
    for (const auto& part : p) {
        if (part == "..")
            return false;
    }
    return true;
}

void send_error(Connection& conn, proto::ErrorCode code, const std::string& msg) {
    const auto enc = proto::encode_error(proto::ErrorMsg{.code = code, .message = msg});
    (void)conn.send_frame(proto::FrameType::Error, enc);
}

}  // namespace

void Server::open_acceptor_(const asio::ip::tcp::endpoint& endpoint) {
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
}

Server::Server(asio::io_context& io,
               const asio::ip::tcp::endpoint& endpoint,
               std::filesystem::path root)
    : io_(io), acceptor_(io), root_(std::move(root)) {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);  // best-effort
    open_acceptor_(endpoint);
}

Server::Server(asio::io_context& io,
               const asio::ip::tcp::endpoint& endpoint,
               std::filesystem::path root,
               TlsConfig tls)
    : io_(io), acceptor_(io), root_(std::move(root)), ssl_ctx_(make_server_tls_context(tls)) {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    open_acceptor_(endpoint);
}

uint16_t Server::local_port() const noexcept {
    return acceptor_.local_endpoint().port();
}

bool Server::run_one() {
    asio::error_code ec;
    auto socket = acceptor_.accept(ec);
    if (ec) {
        spdlog::error("accept failed: {}", ec.message());
        return false;
    }
    return handle_session_(std::move(socket));
}

void Server::run() {
    while (!stop_flag_.load(std::memory_order_relaxed)) {
        asio::error_code ec;
        auto socket = acceptor_.accept(ec);
        if (ec) {
            if (stop_flag_.load(std::memory_order_relaxed))
                break;
            spdlog::error("accept failed: {}", ec.message());
            continue;
        }
        // Thread-per-session. Parallel-streams-per-single-file requires a
        // session-token protocol extension and a per-destination lock around
        // .ftxstate updates; that extension is described in DESIGN.md and is
        // not yet implemented. Concurrent transfers of *different* files are
        // already supported here.
        std::thread([this, sock = std::move(socket)]() mutable {
            (void)handle_session_(std::move(sock));
        }).detach();
    }
}

void Server::stop() {
    stop_flag_.store(true, std::memory_order_relaxed);
    asio::error_code ec;
    acceptor_.close(ec);
}

bool Server::resolve_dest_(const std::string& rel_path, std::filesystem::path& out_resolved) const {
    if (!is_path_safe(rel_path))
        return false;
    auto candidate = (root_ / rel_path).lexically_normal();

    // Defensive: ensure the resolved path begins with root_'s normalized form.
    const auto root_norm = root_.lexically_normal();
    auto cand_str = candidate.generic_string();
    auto root_str = root_norm.generic_string();
    if (cand_str.rfind(root_str, 0) != 0)
        return false;

    out_resolved = std::move(candidate);
    return true;
}

bool Server::handle_session_(asio::ip::tcp::socket socket) {
    auto conn = ssl_ctx_ ? Connection(std::move(socket), *ssl_ctx_) : Connection(std::move(socket));
    if (ssl_ctx_) {
        if (!conn.tls_server_handshake()) {
            spdlog::warn("session: TLS handshake failed: {}", conn.last_error());
            return false;
        }
    }

    // 1. HELLO exchange.
    auto hello_frame = conn.recv_frame();
    if (!hello_frame || hello_frame->header.type != proto::FrameType::Hello) {
        spdlog::warn("session: expected HELLO");
        return false;
    }
    const auto client_hello = proto::decode_hello(hello_frame->payload);
    if (!client_hello) {
        send_error(conn, proto::ErrorCode::ProtocolViolation, "malformed HELLO");
        return false;
    }
    if (client_hello->protocol_version != proto::kProtocolVersion) {
        send_error(conn,
                   proto::ErrorCode::UnsupportedVersion,
                   "server requires protocol version " + std::to_string(proto::kProtocolVersion));
        return false;
    }

    const proto::HelloMsg server_hello{};
    if (!conn.send_frame(proto::FrameType::Hello, proto::encode_hello(server_hello))) {
        spdlog::warn("session: send HELLO failed: {}", conn.last_error());
        return false;
    }

    // 2. MANIFEST.
    auto man_frame = conn.recv_frame();
    if (!man_frame || man_frame->header.type != proto::FrameType::Manifest) {
        spdlog::warn("session: expected MANIFEST");
        return false;
    }
    const auto manifest = proto::decode_manifest(man_frame->payload);
    if (!manifest) {
        send_error(conn, proto::ErrorCode::ProtocolViolation, "malformed MANIFEST");
        return false;
    }

    std::filesystem::path dest;
    if (!resolve_dest_(manifest->path, dest)) {
        send_error(conn, proto::ErrorCode::InvalidPath, "rejected path: " + manifest->path);
        return false;
    }

    spdlog::info("session: receiving \"{}\" → {} ({} bytes, {} chunks)",
                 manifest->path,
                 dest.string(),
                 manifest->file_size,
                 manifest->chunk_count);

    // ---- Resume support: load .ftxstate, reconcile against manifest ----
    auto state_path = dest;
    state_path += ".ftxstate";

    // Manifest identity = BLAKE3(encoded manifest payload). Stable across
    // sessions for the same logical transfer.
    io::ResumeState::ManifestId mid{};
    {
        const auto enc = proto::encode_manifest(*manifest);
        const auto h = blake3(enc);
        std::copy(h.begin(), h.end(), mid.begin());
    }

    io::ResumeState state;
    bool resume_active = false;
    {
        io::ResumeState loaded;
        if (io::ResumeState::load(state_path, loaded) && loaded.manifest_id() == mid &&
            loaded.chunk_count() == manifest->chunk_count) {
            state = std::move(loaded);
            resume_active = true;
            spdlog::info("session: resuming with {} of {} chunks already received",
                         manifest->chunk_count - state.missing().size(),
                         manifest->chunk_count);
        } else {
            // Either no state file, mismatched manifest, or differing chunk
            // count — restart from scratch. Wipe any stale .ftxstate.
            io::ResumeState::remove(state_path);
            state = io::ResumeState(mid, manifest->chunk_count);
        }
    }

    io::FileSink sink(dest);
    if (!sink.open(manifest->file_size, /*resume_existing=*/resume_active)) {
        send_error(conn, proto::ErrorCode::InternalError, sink.last_error());
        return false;
    }

    // ---- Send REQ_CHUNKS with the indices we still need ----
    proto::ReqChunksMsg req;
    req.indices = state.missing();
    if (!conn.send_frame(proto::FrameType::ReqChunks, proto::encode_req_chunks(req))) {
        spdlog::warn("session: send REQ_CHUNKS failed: {}", conn.last_error());
        return false;
    }

    // 3. CHUNK loop until COMPLETE. Hash each chunk against the manifest's
    //    declared hash, accumulate the root hash incrementally for chunks that
    //    arrive in order (which is the phase 2/3 case). For out-of-order
    //    arrivals (phase 5 resume) we recompute the root hash from disk after
    //    the loop.
    uint64_t bytes_received = 0;
    uint32_t chunks_received = 0;
    Blake3Hasher inorder_root_hasher;
    bool all_in_order = !resume_active;  // resume always re-hashes from disk
    uint32_t next_expected = 0;
    proto::Hash client_final_root_hash{};
    while (true) {
        auto frame = conn.recv_frame();
        if (!frame) {
            spdlog::warn("session: recv failed mid-transfer: {}", conn.last_error());
            return false;
        }
        if (frame->header.type == proto::FrameType::Complete) {
            const auto complete = proto::decode_complete(frame->payload);
            if (!complete) {
                send_error(conn, proto::ErrorCode::ProtocolViolation, "malformed COMPLETE");
                return false;
            }
            client_final_root_hash = complete->final_root_hash;
            break;
        }
        if (frame->header.type != proto::FrameType::Chunk) {
            send_error(conn, proto::ErrorCode::ProtocolViolation, "expected CHUNK or COMPLETE");
            return false;
        }

        const auto chunk = proto::decode_chunk(frame->payload);
        if (!chunk) {
            send_error(conn, proto::ErrorCode::ProtocolViolation, "malformed CHUNK");
            return false;
        }
        if (chunk->index >= manifest->chunk_count) {
            send_error(conn, proto::ErrorCode::ProtocolViolation, "chunk index out of range");
            return false;
        }

        // Per-chunk integrity check against BLAKE3(data).
        const auto computed = blake3(chunk->data);
        if (computed != chunk->hash) {
            send_error(conn,
                       proto::ErrorCode::HashMismatch,
                       "chunk hash mismatch at index " + std::to_string(chunk->index));
            return false;
        }
        // Cross-check against the manifest's declared chunk hash.
        if (manifest->chunk_hashes[chunk->index] != chunk->hash) {
            send_error(
                conn,
                proto::ErrorCode::HashMismatch,
                "chunk hash diverges from manifest at index " + std::to_string(chunk->index));
            return false;
        }

        if (all_in_order && chunk->index == next_expected) {
            inorder_root_hasher.update(chunk->data);
            ++next_expected;
        } else {
            all_in_order = false;
        }

        const uint64_t offset = static_cast<uint64_t>(chunk->index) * manifest->chunk_size;
        if (!sink.write_at(offset, chunk->data)) {
            send_error(conn, proto::ErrorCode::InternalError, sink.last_error());
            return false;
        }
        state.mark_received(chunk->index);
        // Persist resume state every chunk so a crash mid-flight doesn't lose
        // ground. For very small chunks this could become a hotspot — phase 6
        // can batch persistence.
        (void)state.save(state_path);

        bytes_received += chunk->data.size();
        ++chunks_received;
    }

    // 4. Verify size, root hash, finalize, ACK.
    // (For a resumed transfer, only the missing chunks are accounted in this
    //  session; we instead verify the full chunk inventory is present.)
    if (resume_active) {
        if (!state.complete()) {
            send_error(conn,
                       proto::ErrorCode::ProtocolViolation,
                       "transfer ended with chunks still missing");
            return false;
        }
    } else {
        if (bytes_received != manifest->file_size) {
            send_error(conn,
                       proto::ErrorCode::ProtocolViolation,
                       "byte count mismatch (expected " + std::to_string(manifest->file_size) +
                           ", got " + std::to_string(bytes_received) + ")");
            return false;
        }
        if (chunks_received != manifest->chunk_count) {
            send_error(conn, proto::ErrorCode::ProtocolViolation, "chunk count mismatch");
            return false;
        }
    }

    proto::Hash actual_root_hash{};
    if (all_in_order) {
        actual_root_hash = inorder_root_hasher.finalize();
    } else {
        // Out-of-order arrival — recompute by reading back from disk.
        // (Triggered by phase-5 resume; phase 3 always takes the in-order path.)
        Blake3Hasher disk_hasher;
        if (!sink.flush()) {
            send_error(conn, proto::ErrorCode::InternalError, sink.last_error());
            return false;
        }
        std::ifstream fb(sink.partial_path(), std::ios::binary);
        if (!fb.is_open()) {
            send_error(
                conn, proto::ErrorCode::InternalError, "cannot reopen .partial for verification");
            return false;
        }
        std::vector<std::byte> rb(64 * 1024);
        while (fb.good()) {
            fb.read(reinterpret_cast<char*>(rb.data()), static_cast<std::streamsize>(rb.size()));
            const auto n = static_cast<size_t>(fb.gcount());
            if (n > 0)
                disk_hasher.update(std::span<const std::byte>(rb.data(), n));
        }
        actual_root_hash = disk_hasher.finalize();
    }

    if (actual_root_hash != manifest->root_hash) {
        send_error(conn, proto::ErrorCode::HashMismatch, "root hash mismatch (manifest)");
        return false;
    }
    if (actual_root_hash != client_final_root_hash) {
        send_error(conn, proto::ErrorCode::HashMismatch, "root hash mismatch (COMPLETE)");
        return false;
    }

    if (!sink.flush() || !sink.finalize()) {
        send_error(conn, proto::ErrorCode::InternalError, sink.last_error());
        return false;
    }
    io::ResumeState::remove(state_path);

    const proto::AckMsg ack{.last_index =
                                (manifest->chunk_count == 0) ? 0 : manifest->chunk_count - 1};
    if (!conn.send_frame(proto::FrameType::Ack, proto::encode_ack(ack))) {
        spdlog::warn("session: ACK send failed: {}", conn.last_error());
        return false;
    }

    spdlog::info("session: complete — {} bytes received", bytes_received);
    return true;
}

}  // namespace ftx::transport
