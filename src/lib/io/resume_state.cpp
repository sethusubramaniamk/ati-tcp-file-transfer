#include "ftx/io/resume_state.hpp"

#include <cstring>
#include <fstream>
#include <system_error>

namespace ftx::io {

namespace {
constexpr std::array<char, 4> kMagic{'F', 'T', 'X', 'S'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderBytes = 4 + 1 + 32 + 4;  // magic+ver+id+count
}  // namespace

ResumeState::ResumeState(ManifestId id, uint32_t chunk_count)
    : manifest_id_(id),
      chunk_count_(chunk_count),
      bitmap_(static_cast<size_t>((chunk_count + 7u) / 8u), 0u) {}

bool ResumeState::is_set(uint32_t i) const noexcept {
    if (i >= chunk_count_)
        return false;
    return (bitmap_[i / 8u] & (1u << (i % 8u))) != 0u;
}

void ResumeState::mark_received(uint32_t i) noexcept {
    if (i >= chunk_count_)
        return;
    bitmap_[i / 8u] = static_cast<uint8_t>(bitmap_[i / 8u] | (1u << (i % 8u)));
}

std::vector<uint32_t> ResumeState::missing() const {
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < chunk_count_; ++i) {
        if (!is_set(i))
            out.push_back(i);
    }
    return out;
}

bool ResumeState::complete() const noexcept {
    for (uint32_t i = 0; i < chunk_count_; ++i) {
        if (!is_set(i))
            return false;
    }
    return true;
}

bool ResumeState::save(const std::filesystem::path& path) const {
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            return false;
        f.write(kMagic.data(), 4);
        const char ver = static_cast<char>(kVersion);
        f.write(&ver, 1);
        f.write(reinterpret_cast<const char*>(manifest_id_.data()),
                static_cast<std::streamsize>(manifest_id_.size()));
        const uint32_t cc_le = chunk_count_;
        const char cc[4] = {
            static_cast<char>(cc_le & 0xFFu),
            static_cast<char>((cc_le >> 8) & 0xFFu),
            static_cast<char>((cc_le >> 16) & 0xFFu),
            static_cast<char>((cc_le >> 24) & 0xFFu),
        };
        f.write(cc, 4);
        if (!bitmap_.empty()) {
            f.write(reinterpret_cast<const char*>(bitmap_.data()),
                    static_cast<std::streamsize>(bitmap_.size()));
        }
        if (!f.good())
            return false;
        f.flush();
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
        if (ec)
            return false;
    }
    return true;
}

bool ResumeState::load(const std::filesystem::path& path, ResumeState& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    char magic[4]{};
    f.read(magic, 4);
    if (f.gcount() != 4 || std::memcmp(magic, kMagic.data(), 4) != 0)
        return false;

    char ver = 0;
    f.read(&ver, 1);
    if (f.gcount() != 1 || static_cast<uint8_t>(ver) != kVersion)
        return false;

    ManifestId id{};
    f.read(reinterpret_cast<char*>(id.data()), static_cast<std::streamsize>(id.size()));
    if (static_cast<size_t>(f.gcount()) != id.size())
        return false;

    char cc_bytes[4]{};
    f.read(cc_bytes, 4);
    if (f.gcount() != 4)
        return false;
    const uint32_t cc = static_cast<uint32_t>(static_cast<unsigned char>(cc_bytes[0])) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(cc_bytes[1])) << 8) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(cc_bytes[2])) << 16) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(cc_bytes[3])) << 24);

    const size_t bitmap_bytes = (cc + 7u) / 8u;
    std::vector<uint8_t> bitmap(bitmap_bytes);
    if (bitmap_bytes > 0) {
        f.read(reinterpret_cast<char*>(bitmap.data()), static_cast<std::streamsize>(bitmap_bytes));
        if (static_cast<size_t>(f.gcount()) != bitmap_bytes)
            return false;
    }

    out.manifest_id_ = id;
    out.chunk_count_ = cc;
    out.bitmap_ = std::move(bitmap);
    (void)kHeaderBytes;
    return true;
}

void ResumeState::remove(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

}  // namespace ftx::io
