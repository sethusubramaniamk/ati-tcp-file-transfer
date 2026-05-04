#include "ftx/transport/server.hpp"

#include <spdlog/spdlog.h>

#include <system_error>

#include "ftx/io/file_sink.hpp"
#include "ftx/proto/messages.hpp"
#include "ftx/transport/connection.hpp"

namespace ftx::transport {

namespace {

// Reject paths that are absolute, contain ".." segments, or end up outside
// the configured root directory.
bool is_path_safe(const std::string& rel) {
    if (rel.empty()) return false;
    const std::filesystem::path p(rel);
    if (p.is_absolute()) return false;
    for (const auto& part : p) {
        if (part == "..") return false;
    }
    return true;
}

void send_error(Connection& conn, proto::ErrorCode code, const std::string& msg) {
    const auto enc = proto::encode_error(proto::ErrorMsg{.code = code, .message = msg});
    (void)conn.send_frame(proto::FrameType::Error, enc);
}

}  // namespace

Server::Server(asio::io_context& io,
               const asio::ip::tcp::endpoint& endpoint,
               std::filesystem::path root)
    : io_(io),
      acceptor_(io),
      root_(std::move(root)) {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);  // best-effort

    // Open + set SO_REUSEADDR + bind + listen, so we don't get "Address already
    // in use" for ~60s after a previous run on the same port.
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
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
            if (stop_flag_.load(std::memory_order_relaxed)) break;
            spdlog::error("accept failed: {}", ec.message());
            continue;
        }
        (void)handle_session_(std::move(socket));
    }
}

void Server::stop() {
    stop_flag_.store(true, std::memory_order_relaxed);
    asio::error_code ec;
    acceptor_.close(ec);
}

bool Server::resolve_dest_(const std::string& rel_path,
                           std::filesystem::path& out_resolved) const {
    if (!is_path_safe(rel_path)) return false;
    auto candidate = (root_ / rel_path).lexically_normal();

    // Defensive: ensure the resolved path begins with root_'s normalized form.
    const auto root_norm = root_.lexically_normal();
    auto       cand_str  = candidate.generic_string();
    auto       root_str  = root_norm.generic_string();
    if (cand_str.rfind(root_str, 0) != 0) return false;

    out_resolved = std::move(candidate);
    return true;
}

bool Server::handle_session_(asio::ip::tcp::socket socket) {
    Connection conn(std::move(socket));

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
        send_error(conn, proto::ErrorCode::UnsupportedVersion,
                   "server requires protocol version " +
                       std::to_string(proto::kProtocolVersion));
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
                 manifest->path, dest.string(), manifest->file_size, manifest->chunk_count);

    io::FileSink sink(dest);
    if (!sink.open(manifest->file_size)) {
        send_error(conn, proto::ErrorCode::InternalError, sink.last_error());
        return false;
    }

    // 3. CHUNK loop until COMPLETE.
    uint64_t bytes_received = 0;
    uint32_t chunks_received = 0;
    while (true) {
        auto frame = conn.recv_frame();
        if (!frame) {
            spdlog::warn("session: recv failed mid-transfer: {}", conn.last_error());
            return false;
        }
        if (frame->header.type == proto::FrameType::Complete) {
            // Done — fall through to finalization.
            const auto complete = proto::decode_complete(frame->payload);
            if (!complete) {
                send_error(conn, proto::ErrorCode::ProtocolViolation, "malformed COMPLETE");
                return false;
            }
            break;
        }
        if (frame->header.type != proto::FrameType::Chunk) {
            send_error(conn, proto::ErrorCode::ProtocolViolation,
                       "expected CHUNK or COMPLETE");
            return false;
        }

        const auto chunk = proto::decode_chunk(frame->payload);
        if (!chunk) {
            send_error(conn, proto::ErrorCode::ProtocolViolation, "malformed CHUNK");
            return false;
        }
        if (chunk->index >= manifest->chunk_count) {
            send_error(conn, proto::ErrorCode::ProtocolViolation,
                       "chunk index out of range");
            return false;
        }

        // Phase 3 will verify chunk->hash here against BLAKE3(data).

        const uint64_t offset =
            static_cast<uint64_t>(chunk->index) * manifest->chunk_size;
        if (!sink.write_at(offset, chunk->data)) {
            send_error(conn, proto::ErrorCode::InternalError, sink.last_error());
            return false;
        }
        bytes_received += chunk->data.size();
        ++chunks_received;
    }

    // 4. Verify size, finalize, ACK.
    if (bytes_received != manifest->file_size) {
        send_error(conn, proto::ErrorCode::ProtocolViolation,
                   "byte count mismatch (expected " +
                       std::to_string(manifest->file_size) + ", got " +
                       std::to_string(bytes_received) + ")");
        return false;
    }
    if (chunks_received != manifest->chunk_count) {
        send_error(conn, proto::ErrorCode::ProtocolViolation, "chunk count mismatch");
        return false;
    }
    if (!sink.flush() || !sink.finalize()) {
        send_error(conn, proto::ErrorCode::InternalError, sink.last_error());
        return false;
    }

    const proto::AckMsg ack{.last_index = (manifest->chunk_count == 0)
                                              ? 0
                                              : manifest->chunk_count - 1};
    if (!conn.send_frame(proto::FrameType::Ack, proto::encode_ack(ack))) {
        spdlog::warn("session: ACK send failed: {}", conn.last_error());
        return false;
    }

    spdlog::info("session: complete — {} bytes received", bytes_received);
    return true;
}

}  // namespace ftx::transport
