#pragma once

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "ftx/transport/tls.hpp"

namespace ftx::transport {

// File-receiver server. Phase 2: serial — accepts and handles one session at
// a time. Optional TLS via TlsConfig.
class Server {
 public:
    // Plain TCP variant.
    Server(asio::io_context&              io,
           const asio::ip::tcp::endpoint& endpoint,
           std::filesystem::path          root);

    // TLS variant. The server presents `tls.cert_path` and, when verify_peer
    // is true, requires a peer certificate chained to `tls.ca_path` (mTLS).
    Server(asio::io_context&              io,
           const asio::ip::tcp::endpoint& endpoint,
           std::filesystem::path          root,
           TlsConfig                      tls);

    [[nodiscard]] uint16_t local_port() const noexcept;

    [[nodiscard]] bool run_one();
    void run();
    void stop();

 private:
    [[nodiscard]] bool handle_session_(asio::ip::tcp::socket socket);
    [[nodiscard]] bool resolve_dest_(const std::string& rel_path,
                                     std::filesystem::path& out_resolved) const;

    void open_acceptor_(const asio::ip::tcp::endpoint& endpoint);

    asio::io_context&                   io_;
    asio::ip::tcp::acceptor             acceptor_;
    std::filesystem::path               root_;
    std::unique_ptr<asio::ssl::context> ssl_ctx_;  // null when plain
    std::atomic<bool>                   stop_flag_{false};
};

}  // namespace ftx::transport
