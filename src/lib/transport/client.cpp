#include "ftx/transport/client.hpp"

#include <spdlog/spdlog.h>

#include <vector>

#include "ftx/io/file_source.hpp"
#include "ftx/proto/messages.hpp"
#include "ftx/transport/connection.hpp"

namespace ftx::transport {

Client::Client(asio::io_context& io) noexcept : io_(io) {}

Client::Client(asio::io_context& io, Options opts) noexcept
    : io_(io), opts_(opts) {}

bool Client::send(const std::string& host,
                  uint16_t port,
                  const std::filesystem::path& source_path,
                  const std::string& remote_dest) {
    // 1. Open source file.
    io::FileSource src(source_path);
    if (!src.open()) {
        last_error_ = "open source: " + src.last_error();
        return false;
    }
    const uint64_t file_size = src.size();
    const uint32_t chunk_size = opts_.chunk_size;
    const uint32_t chunk_count =
        (file_size == 0) ? 0
                         : static_cast<uint32_t>((file_size + chunk_size - 1) / chunk_size);

    // 2. Resolve and connect.
    asio::ip::tcp::resolver resolver(io_);
    asio::error_code        ec;
    auto                    endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec) {
        last_error_ = "resolve: " + ec.message();
        return false;
    }

    asio::ip::tcp::socket socket(io_);
    asio::connect(socket, endpoints, ec);
    if (ec) {
        last_error_ = "connect: " + ec.message();
        return false;
    }

    Connection conn(std::move(socket));

    // 3. HELLO exchange.
    if (!conn.send_frame(proto::FrameType::Hello, proto::encode_hello(proto::HelloMsg{}))) {
        last_error_ = "send HELLO: " + conn.last_error();
        return false;
    }
    auto hello_resp = conn.recv_frame();
    if (!hello_resp || hello_resp->header.type != proto::FrameType::Hello) {
        last_error_ = "recv HELLO: " +
                      (hello_resp ? "unexpected frame type" : conn.last_error());
        return false;
    }
    const auto server_hello = proto::decode_hello(hello_resp->payload);
    if (!server_hello) {
        last_error_ = "decode HELLO: malformed";
        return false;
    }
    if (server_hello->protocol_version != proto::kProtocolVersion) {
        last_error_ = "protocol version mismatch";
        return false;
    }

    // 4. Send MANIFEST. Phase 2: hashes all zero.
    proto::ManifestMsg manifest;
    manifest.file_size   = file_size;
    manifest.chunk_size  = chunk_size;
    manifest.chunk_count = chunk_count;
    manifest.path        = remote_dest;
    manifest.chunk_hashes.resize(chunk_count, proto::kZeroHash);
    // root_hash stays zero in phase 2.

    if (!conn.send_frame(proto::FrameType::Manifest, proto::encode_manifest(manifest))) {
        last_error_ = "send MANIFEST: " + conn.last_error();
        return false;
    }

    // 5. CHUNK loop.
    std::vector<std::byte> buf(chunk_size);
    for (uint32_t i = 0; i < chunk_count; ++i) {
        const uint64_t offset = static_cast<uint64_t>(i) * chunk_size;
        size_t         n      = 0;
        if (!src.read_at(offset, buf, &n)) {
            last_error_ = "read source: " + src.last_error();
            return false;
        }
        proto::ChunkMsg chunk;
        chunk.index = i;
        chunk.data.assign(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
        // chunk.hash stays zero in phase 2.

        if (!conn.send_frame(proto::FrameType::Chunk, proto::encode_chunk(chunk))) {
            last_error_ = "send CHUNK[" + std::to_string(i) + "]: " + conn.last_error();
            return false;
        }
    }

    // 6. COMPLETE.
    if (!conn.send_frame(proto::FrameType::Complete,
                         proto::encode_complete(proto::CompleteMsg{}))) {
        last_error_ = "send COMPLETE: " + conn.last_error();
        return false;
    }

    // 7. ACK.
    auto ack_frame = conn.recv_frame();
    if (!ack_frame) {
        last_error_ = "recv ACK: " + conn.last_error();
        return false;
    }
    if (ack_frame->header.type == proto::FrameType::Error) {
        const auto err = proto::decode_error(ack_frame->payload);
        last_error_ = err ? ("server ERROR: " + err->message) : "server ERROR (malformed)";
        return false;
    }
    if (ack_frame->header.type != proto::FrameType::Ack) {
        last_error_ = "expected ACK, got something else";
        return false;
    }

    spdlog::info("send complete: {} bytes in {} chunks", file_size, chunk_count);
    return true;
}

}  // namespace ftx::transport
