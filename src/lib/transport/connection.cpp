#include "ftx/transport/connection.hpp"

#include <array>
#include <type_traits>
#include <vector>

namespace ftx::transport {

namespace {

template<typename Stream>
constexpr bool is_tls_stream_v = std::is_same_v<std::decay_t<Stream>, Connection::TlsStream>;

// Best-effort close: shutdown both directions, then close the underlying
// socket. For TLS streams, attempt a graceful TLS shutdown first but ignore
// errors (peer may have already gone away).
template<typename Stream>
void shutdown_and_close(Stream& s) noexcept {
    asio::error_code ec;
    if constexpr (is_tls_stream_v<Stream>) {
        s.shutdown(ec);
        if (s.lowest_layer().is_open()) {
            s.lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            s.lowest_layer().close(ec);
        }
    } else {
        if (s.is_open()) {
            s.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            s.close(ec);
        }
    }
}

}  // namespace

Connection::Connection(PlainStream socket) noexcept : stream_(std::move(socket)) {}

Connection::Connection(PlainStream socket, asio::ssl::context& ctx)
    : stream_(TlsStream{std::move(socket), ctx}) {}

Connection::~Connection() {
    close();
}

bool Connection::tls_client_handshake(std::string_view sni_host) {
    if (!std::holds_alternative<TlsStream>(stream_))
        return true;
    auto& s = std::get<TlsStream>(stream_);

    if (!sni_host.empty()) {
        // Pass server name for SNI extension.
        // SSL_set_tlsext_host_name expands to a C-style cast inside OpenSSL,
        // hence the localized -Wold-style-cast suppression.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
        ::SSL_set_tlsext_host_name(s.native_handle(), std::string(sni_host).c_str());
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    }

    asio::error_code ec;
    s.handshake(asio::ssl::stream_base::client, ec);
    if (ec) {
        last_error_ = "tls client handshake: " + ec.message();
        return false;
    }
    return true;
}

bool Connection::tls_server_handshake() {
    if (!std::holds_alternative<TlsStream>(stream_))
        return true;
    auto& s = std::get<TlsStream>(stream_);
    asio::error_code ec;
    s.handshake(asio::ssl::stream_base::server, ec);
    if (ec) {
        last_error_ = "tls server handshake: " + ec.message();
        return false;
    }
    return true;
}

bool Connection::send_frame(proto::FrameType type, std::span<const std::byte> payload) {
    if (payload.size() > proto::kDefaultMaxPayload) {
        last_error_ = "send_frame: payload exceeds maximum";
        return false;
    }

    std::array<std::byte, proto::kHeaderSize> header_buf{};
    const proto::FrameHeader hdr{
        .payload_len = static_cast<uint32_t>(payload.size()),
        .type = type,
    };
    hdr.encode_to(header_buf);

    const std::array<asio::const_buffer, 2> bufs{
        asio::buffer(header_buf.data(), header_buf.size()),
        asio::buffer(payload.data(), payload.size()),
    };

    asio::error_code ec;
    std::visit([&](auto& s) { asio::write(s, bufs, ec); }, stream_);
    if (ec) {
        last_error_ = "send_frame: " + ec.message();
        return false;
    }
    return true;
}

std::optional<proto::Frame> Connection::recv_frame(size_t max_payload) {
    std::array<std::byte, proto::kHeaderSize> header_buf{};
    asio::error_code ec;
    std::visit(
        [&](auto& s) { asio::read(s, asio::buffer(header_buf.data(), header_buf.size()), ec); },
        stream_);
    if (ec) {
        last_error_ = "recv_frame header: " + ec.message();
        return std::nullopt;
    }

    proto::FrameHeader::DecodeError reason{};
    auto hdr = proto::FrameHeader::decode_from(header_buf, max_payload, &reason);
    if (!hdr.has_value()) {
        switch (reason) {
            case proto::FrameHeader::DecodeError::BadCrc:
                last_error_ = "recv_frame: header CRC mismatch";
                break;
            case proto::FrameHeader::DecodeError::UnknownType:
                last_error_ = "recv_frame: unknown frame type";
                break;
            case proto::FrameHeader::DecodeError::PayloadTooLarge:
                last_error_ = "recv_frame: payload too large";
                break;
        }
        return std::nullopt;
    }

    proto::Frame f;
    f.header = *hdr;
    if (hdr->payload_len > 0) {
        f.payload.resize(hdr->payload_len);
        std::visit(
            [&](auto& s) { asio::read(s, asio::buffer(f.payload.data(), f.payload.size()), ec); },
            stream_);
        if (ec) {
            last_error_ = "recv_frame payload: " + ec.message();
            return std::nullopt;
        }
    }
    return f;
}

void Connection::close() noexcept {
    std::visit([](auto& s) { shutdown_and_close(s); }, stream_);
}

bool Connection::is_open() const noexcept {
    return std::visit(
        [](const auto& s) -> bool {
            using S = std::decay_t<decltype(s)>;
            if constexpr (is_tls_stream_v<S>) {
                return s.lowest_layer().is_open();
            } else {
                return s.is_open();
            }
        },
        stream_);
}

bool Connection::is_tls() const noexcept {
    return std::holds_alternative<TlsStream>(stream_);
}

}  // namespace ftx::transport
