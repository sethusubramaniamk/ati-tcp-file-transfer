#include "ftx/transport/client.hpp"

#include <spdlog/spdlog.h>

#include <vector>

#include <memory>

#include "ftx/io/file_source.hpp"
#include "ftx/proto/messages.hpp"
#include "ftx/transport/connection.hpp"
#include "ftx/transport/tls.hpp"
#include "ftx/util/blake3.hpp"

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

    std::unique_ptr<asio::ssl::context> ssl_ctx;
    if (opts_.tls.has_value()) {
        ssl_ctx = make_client_tls_context(*opts_.tls);
    }

    auto conn = ssl_ctx ? Connection(std::move(socket), *ssl_ctx)
                        : Connection(std::move(socket));
    if (ssl_ctx) {
        const std::string sni = opts_.tls->sni_host.empty() ? host : opts_.tls->sni_host;
        if (!conn.tls_client_handshake(sni)) {
            last_error_ = "TLS handshake: " + conn.last_error();
            return false;
        }
    }

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

    // 4. First pass: read the file once to compute per-chunk hashes and the
    //    whole-file root hash. We pay an extra read of the source here so the
    //    MANIFEST can carry the full hash inventory upfront — this enables the
    //    receiver to verify each chunk on arrival and the resume protocol
    //    (phase 5) to ask for specific missing chunks. Phase 6 will fold
    //    this into a single streaming pass.
    std::vector<proto::Hash> chunk_hashes(chunk_count);
    Blake3Hasher             root_hasher;
    {
        std::vector<std::byte> scratch(chunk_size);
        for (uint32_t i = 0; i < chunk_count; ++i) {
            const uint64_t offset = static_cast<uint64_t>(i) * chunk_size;
            size_t         n      = 0;
            if (!src.read_at(offset, scratch, &n)) {
                last_error_ = "hash pass: " + src.last_error();
                return false;
            }
            const auto chunk_view = std::span<const std::byte>(scratch.data(), n);
            chunk_hashes[i]       = blake3(chunk_view);
            root_hasher.update(chunk_view);
        }
    }
    const proto::Hash root_hash = root_hasher.finalize();

    proto::ManifestMsg manifest;
    manifest.file_size    = file_size;
    manifest.chunk_size   = chunk_size;
    manifest.chunk_count  = chunk_count;
    manifest.root_hash    = root_hash;
    manifest.path         = remote_dest;
    manifest.chunk_hashes = chunk_hashes;

    if (!conn.send_frame(proto::FrameType::Manifest, proto::encode_manifest(manifest))) {
        last_error_ = "send MANIFEST: " + conn.last_error();
        return false;
    }

    // 4b. Wait for REQ_CHUNKS — receiver tells us which indices to send.
    auto req_frame = conn.recv_frame();
    if (!req_frame || req_frame->header.type != proto::FrameType::ReqChunks) {
        last_error_ = "recv REQ_CHUNKS: " +
                      (req_frame ? "unexpected frame type" : conn.last_error());
        return false;
    }
    const auto req = proto::decode_req_chunks(req_frame->payload);
    if (!req) {
        last_error_ = "decode REQ_CHUNKS: malformed";
        return false;
    }

    // 5. CHUNK loop — send only the chunks the receiver asked for. For a
    //    fresh transfer this is the full range [0, chunk_count); on resume it
    //    is just the missing indices.
    std::vector<std::byte> buf(chunk_size);
    for (const uint32_t i : req->indices) {
        if (i >= chunk_count) {
            last_error_ = "REQ_CHUNKS: index " + std::to_string(i) + " out of range";
            return false;
        }
        const uint64_t offset = static_cast<uint64_t>(i) * chunk_size;
        size_t         n      = 0;
        if (!src.read_at(offset, buf, &n)) {
            last_error_ = "read source: " + src.last_error();
            return false;
        }
        proto::ChunkMsg chunk;
        chunk.index = i;
        chunk.hash  = chunk_hashes[i];
        chunk.data.assign(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));

        if (!conn.send_frame(proto::FrameType::Chunk, proto::encode_chunk(chunk))) {
            last_error_ = "send CHUNK[" + std::to_string(i) + "]: " + conn.last_error();
            return false;
        }
    }

    // 6. COMPLETE — carries the same root_hash for end-of-stream verification.
    proto::CompleteMsg complete;
    complete.final_root_hash = root_hash;
    complete.status          = 0;
    if (!conn.send_frame(proto::FrameType::Complete, proto::encode_complete(complete))) {
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
