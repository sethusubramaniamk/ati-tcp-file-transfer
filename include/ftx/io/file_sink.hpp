#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace ftx::io {

// Write-side random-access file. Opens a `.partial` file alongside the
// destination; finalize() atomically renames it into place. Pre-allocates
// the file to the requested size (sparse where supported).
class FileSink {
 public:
    explicit FileSink(std::filesystem::path final_path);

    // Open the .partial file and pre-size it to `total_size`.
    // When `resume_existing` is true, an existing .partial of size >=
    // total_size is preserved (no truncation); a smaller or absent file falls
    // back to a fresh pre-sized creation.
    [[nodiscard]] bool open(uint64_t total_size, bool resume_existing = false);

    [[nodiscard]] bool is_open() const noexcept { return stream_.is_open(); }

    // Random-access write. Caller is responsible for chunk ordering /
    // overlap correctness.
    [[nodiscard]] bool write_at(uint64_t offset, std::span<const std::byte> data);

    [[nodiscard]] bool flush();

    // Close the .partial file, then atomic-rename it into final_path. Removes
    // any pre-existing target. Returns false on failure with last_error() set.
    [[nodiscard]] bool finalize();

    // Close without rename; .partial remains on disk for resume.
    void close();

    [[nodiscard]] std::string last_error() const { return last_error_; }
    [[nodiscard]] const std::filesystem::path& final_path()   const noexcept { return final_path_; }
    [[nodiscard]] const std::filesystem::path& partial_path() const noexcept { return partial_path_; }

 private:
    std::filesystem::path final_path_;
    std::filesystem::path partial_path_;
    std::ofstream         stream_;        // ofstream + seekp gives us random write
    uint64_t              total_size_ = 0;
    std::string           last_error_;
};

}  // namespace ftx::io
