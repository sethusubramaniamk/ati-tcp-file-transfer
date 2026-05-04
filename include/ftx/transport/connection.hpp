#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include "ftx/proto/frame.hpp"
#include "ftx/proto/types.hpp"

namespace ftx::transport {

// Synchronous frame-oriented wrapper over either a plain TCP socket or a
// TLS-wrapped TCP socket. One Connection per session; not thread-safe.
class Connection {
 public:
    using PlainStream = asio::ip::tcp::socket;
    using TlsStream = asio::ssl::stream<asio::ip::tcp::socket>;

    // Plain TCP construction.
    explicit Connection(PlainStream socket) noexcept;

    // TLS construction. The caller retains ownership of `ctx`; it must outlive
    // this Connection. Caller must follow with tls_client_handshake() or
    // tls_server_handshake() before sending or receiving frames.
    Connection(PlainStream socket, asio::ssl::context& ctx);

    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    // TLS handshake helpers. No-ops on plain connections (return true).
    [[nodiscard]] bool tls_client_handshake(std::string_view sni_host);
    [[nodiscard]] bool tls_server_handshake();

    [[nodiscard]] bool send_frame(proto::FrameType type, std::span<const std::byte> payload);
    [[nodiscard]] std::optional<proto::Frame> recv_frame(
        size_t max_payload = proto::kDefaultMaxPayload);

    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] bool is_tls() const noexcept;

    [[nodiscard]] std::string last_error() const { return last_error_; }

 private:
    using StreamVariant = std::variant<PlainStream, TlsStream>;

    StreamVariant stream_;
    std::string last_error_;
};

}  // namespace ftx::transport
