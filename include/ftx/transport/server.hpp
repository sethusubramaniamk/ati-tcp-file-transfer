#pragma once

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ftx::transport {

// File-receiver server. Accepts connections on a bound endpoint and runs the
// receiver side of the ftx protocol. Phase 2: serial — accepts and handles
// one session at a time.
class Server {
 public:
    // `endpoint` may use port 0 to request an OS-assigned port; the actual
    // port is then available via local_port() after construction.
    Server(asio::io_context&              io,
           const asio::ip::tcp::endpoint& endpoint,
           std::filesystem::path          root);

    [[nodiscard]] uint16_t local_port() const noexcept;

    // Accept exactly one connection, run a session, return. Returns true if
    // the session completed successfully (file received and finalized).
    [[nodiscard]] bool run_one();

    // Accept-loop. Runs sessions back-to-back until stop() is called or an
    // accept error occurs.
    void run();

    // Cause run() to return at the next acceptable point.
    void stop();

 private:
    [[nodiscard]] bool handle_session_(asio::ip::tcp::socket socket);
    [[nodiscard]] bool resolve_dest_(const std::string& rel_path,
                                     std::filesystem::path& out_resolved) const;

    asio::io_context&       io_;
    asio::ip::tcp::acceptor acceptor_;
    std::filesystem::path   root_;
    std::atomic<bool>       stop_flag_{false};
};

}  // namespace ftx::transport
