#include "ftx/io/file_source.hpp"

#include <system_error>

namespace ftx::io {

FileSource::FileSource(std::filesystem::path path) : path_(std::move(path)) {}

bool FileSource::open() {
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        last_error_ = "file does not exist: " + path_.string();
        return false;
    }
    if (!std::filesystem::is_regular_file(path_, ec)) {
        last_error_ = "not a regular file: " + path_.string();
        return false;
    }
    const auto file_size = std::filesystem::file_size(path_, ec);
    if (ec) {
        last_error_ = "failed to stat: " + ec.message();
        return false;
    }
    size_ = static_cast<uint64_t>(file_size);

    stream_.open(path_, std::ios::binary);
    if (!stream_.is_open()) {
        last_error_ = "open() failed for " + path_.string();
        return false;
    }
    return true;
}

bool FileSource::read_at(uint64_t offset, std::span<std::byte> buf, size_t* out_n) {
    if (!stream_.is_open()) {
        last_error_ = "read_at on closed source";
        return false;
    }
    if (offset > size_) {
        last_error_ = "read_at: offset beyond EOF";
        return false;
    }
    const uint64_t available = size_ - offset;
    const size_t to_read = static_cast<size_t>(
        (available < buf.size()) ? available : static_cast<uint64_t>(buf.size()));

    stream_.clear();  // clear any previous EOF state
    stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream_.good()) {
        last_error_ = "seekg failed";
        return false;
    }
    stream_.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(to_read));
    const auto got = static_cast<size_t>(stream_.gcount());
    if (got != to_read && !stream_.eof()) {
        last_error_ = "read short of expected bytes";
        return false;
    }
    if (out_n != nullptr)
        *out_n = got;
    return true;
}

void FileSource::close() {
    if (stream_.is_open())
        stream_.close();
}

}  // namespace ftx::io
