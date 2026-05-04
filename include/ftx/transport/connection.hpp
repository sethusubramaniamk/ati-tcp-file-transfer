#pragma once

#include <asio.hpp>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "ftx/proto/frame.hpp"
#include "ftx/proto/types.hpp"

namespace ftx::transport {

// Synchronous frame-oriented wrapper around an asio TCP socket.
// One Connection per session; not thread-safe.
class Connection {
 public:
    explicit Connection(asio::ip::tcp::socket socket) noexcept;
    ~Connection();

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;

    // Send a single frame (header || payload). Returns false on I/O error;
    // last_error() carries the detail.
    [[nodiscard]] bool send_frame(proto::FrameType type, std::span<const std::byte> payload);

    // Read a single frame, blocking until complete or error. Returns nullopt
    // on EOF or I/O error.
    [[nodiscard]] std::optional<proto::Frame> recv_frame(
        size_t max_payload = proto::kDefaultMaxPayload);

    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] std::string last_error() const { return last_error_; }
    [[nodiscard]] asio::ip::tcp::socket&       socket() noexcept       { return socket_; }
    [[nodiscard]] const asio::ip::tcp::socket& socket() const noexcept { return socket_; }

 private:
    asio::ip::tcp::socket socket_;
    std::string           last_error_;
};

}  // namespace ftx::transport
