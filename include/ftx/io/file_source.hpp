#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace ftx::io {

// Read-only random-access view over a file. Cross-platform via std::ifstream
// in binary mode. Not thread-safe (one source per session).
class FileSource {
 public:
    explicit FileSource(std::filesystem::path path);

    // Returns true on success; on failure, last_error() carries the reason.
    [[nodiscard]] bool open();

    [[nodiscard]] uint64_t size() const noexcept { return size_; }
    [[nodiscard]] bool     is_open() const noexcept { return stream_.is_open(); }

    // Reads up to buf.size() bytes starting at `offset`. The actual count is
    // written to *out_n on success. Returns false on I/O error or out-of-range
    // offset; the source is then in an error state.
    [[nodiscard]] bool read_at(uint64_t offset, std::span<std::byte> buf, size_t* out_n);

    void                      close();
    [[nodiscard]] std::string last_error() const { return last_error_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
    std::filesystem::path path_;
    std::ifstream         stream_;
    uint64_t              size_       = 0;
    std::string           last_error_;
};

}  // namespace ftx::io
