#pragma once

#include <asio.hpp>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ftx::transport {

struct ClientOptions {
    uint32_t chunk_size = 1u * 1024 * 1024;  // 1 MiB default
};

// File-sender client. Phase 2: synchronous, blocking, single transfer per
// instance. Returns true on a complete & ACK'd transfer.
class Client {
 public:
    using Options = ClientOptions;

    explicit Client(asio::io_context& io) noexcept;
    Client(asio::io_context& io, Options opts) noexcept;

    [[nodiscard]] bool send(const std::string&           host,
                            uint16_t                     port,
                            const std::filesystem::path& source_path,
                            const std::string&           remote_dest);

    [[nodiscard]] std::string last_error() const { return last_error_; }

 private:
    asio::io_context& io_;
    Options           opts_;
    std::string       last_error_;
};

}  // namespace ftx::transport
