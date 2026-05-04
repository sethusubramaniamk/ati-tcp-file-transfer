#include "ftx/io/file_sink.hpp"

#include <system_error>

namespace ftx::io {

namespace {
std::filesystem::path with_partial_suffix(const std::filesystem::path& p) {
    auto out = p;
    out += ".partial";
    return out;
}
}  // namespace

FileSink::FileSink(std::filesystem::path final_path)
    : final_path_(std::move(final_path)), partial_path_(with_partial_suffix(final_path_)) {}

bool FileSink::open(uint64_t total_size, bool resume_existing) {
    total_size_ = total_size;

    std::error_code ec;
    std::filesystem::create_directories(final_path_.parent_path(), ec);  // ignore failure here

    if (resume_existing) {
        std::error_code stat_ec;
        const auto cur_size = std::filesystem::exists(partial_path_, stat_ec)
                                  ? std::filesystem::file_size(partial_path_, stat_ec)
                                  : 0;
        if (!stat_ec && cur_size >= total_size) {
            // Reuse the existing .partial. Open in in/out mode (no trunc).
            stream_.open(partial_path_, std::ios::in | std::ios::out | std::ios::binary);
            if (stream_.is_open())
                return true;
            // Fall through and reinitialize.
        }
    }

    stream_.open(partial_path_, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream_.is_open()) {
        last_error_ = "failed to open " + partial_path_.string();
        return false;
    }

    // Pre-size: seek to total_size-1 and write one byte. That makes the file
    // size correct on all platforms (POSIX: sparse; Windows: zero-filled).
    if (total_size > 0) {
        stream_.seekp(static_cast<std::streamoff>(total_size - 1), std::ios::beg);
        const char zero = 0;
        stream_.write(&zero, 1);
        if (!stream_.good()) {
            last_error_ = "failed to pre-size sink to " + std::to_string(total_size);
            return false;
        }
    }
    return true;
}

bool FileSink::write_at(uint64_t offset, std::span<const std::byte> data) {
    if (!stream_.is_open()) {
        last_error_ = "write_at on closed sink";
        return false;
    }
    if (offset + data.size() > total_size_) {
        last_error_ = "write_at: range exceeds declared total size";
        return false;
    }
    stream_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream_.good()) {
        last_error_ = "seekp failed";
        return false;
    }
    stream_.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    if (!stream_.good()) {
        last_error_ = "write failed";
        return false;
    }
    return true;
}

bool FileSink::flush() {
    if (!stream_.is_open())
        return true;
    stream_.flush();
    if (!stream_.good()) {
        last_error_ = "flush failed";
        return false;
    }
    return true;
}

bool FileSink::finalize() {
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
    std::error_code ec;
    std::filesystem::rename(partial_path_, final_path_, ec);
    if (ec) {
        // On Windows, rename fails if destination exists; remove and retry.
        std::filesystem::remove(final_path_, ec);
        std::filesystem::rename(partial_path_, final_path_, ec);
        if (ec) {
            last_error_ = "rename failed: " + ec.message();
            return false;
        }
    }
    return true;
}

void FileSink::close() {
    if (stream_.is_open())
        stream_.close();
}

}  // namespace ftx::io
