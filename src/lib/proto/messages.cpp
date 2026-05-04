#include "ftx/proto/messages.hpp"

#include <algorithm>
#include <cstring>

#include "ftx/util/byteorder.hpp"

namespace ftx::proto {

namespace {

constexpr size_t kHashSize = 32;

// Append helpers — write directly into the target vector via resize+span to
// avoid an intermediate std::array. gcc 11 raises a -Wstringop-overflow false
// positive when it inlines vector::insert(array.begin(), array.end()) deeply.

void append_u8(std::vector<std::byte>& out, uint8_t v) {
    out.push_back(std::byte{v});
}

void append_u16(std::vector<std::byte>& out, uint16_t v) {
    const size_t pos = out.size();
    out.resize(pos + 2);
    write_be_u16(std::span<std::byte, 2>(out.data() + pos, 2), v);
}

void append_u32(std::vector<std::byte>& out, uint32_t v) {
    const size_t pos = out.size();
    out.resize(pos + 4);
    write_be_u32(std::span<std::byte, 4>(out.data() + pos, 4), v);
}

void append_u64(std::vector<std::byte>& out, uint64_t v) {
    const size_t pos = out.size();
    out.resize(pos + 8);
    write_be_u64(std::span<std::byte, 8>(out.data() + pos, 8), v);
}

void append_hash(std::vector<std::byte>& out, const Hash& h) {
    const size_t pos = out.size();
    out.resize(pos + h.size());
    std::memcpy(out.data() + pos, h.data(), h.size());
}

void append_bytes(std::vector<std::byte>& out, std::span<const std::byte> s) {
    if (s.empty())
        return;
    const size_t pos = out.size();
    out.resize(pos + s.size());
    std::memcpy(out.data() + pos, s.data(), s.size());
}

// Cursor-style decode helpers. Each returns false if the payload is too short.

class Cursor {
 public:
    explicit Cursor(std::span<const std::byte> p) noexcept : payload_(p) {}

    [[nodiscard]] bool take_u8(uint8_t& out) noexcept {
        if (pos_ + 1 > payload_.size())
            return false;
        out = std::to_integer<uint8_t>(payload_[pos_]);
        pos_ += 1;
        return true;
    }
    [[nodiscard]] bool take_u16(uint16_t& out) noexcept {
        if (pos_ + 2 > payload_.size())
            return false;
        out = read_be_u16(std::span<const std::byte, 2>(payload_.data() + pos_, 2));
        pos_ += 2;
        return true;
    }
    [[nodiscard]] bool take_u32(uint32_t& out) noexcept {
        if (pos_ + 4 > payload_.size())
            return false;
        out = read_be_u32(std::span<const std::byte, 4>(payload_.data() + pos_, 4));
        pos_ += 4;
        return true;
    }
    [[nodiscard]] bool take_u64(uint64_t& out) noexcept {
        if (pos_ + 8 > payload_.size())
            return false;
        out = read_be_u64(std::span<const std::byte, 8>(payload_.data() + pos_, 8));
        pos_ += 8;
        return true;
    }
    [[nodiscard]] bool take_hash(Hash& out) noexcept {
        if (pos_ + kHashSize > payload_.size())
            return false;
        std::memcpy(out.data(), payload_.data() + pos_, kHashSize);
        pos_ += kHashSize;
        return true;
    }
    [[nodiscard]] bool take_bytes(size_t n, std::vector<std::byte>& out) {
        if (pos_ + n > payload_.size())
            return false;
        out.assign(payload_.begin() + static_cast<std::ptrdiff_t>(pos_),
                   payload_.begin() + static_cast<std::ptrdiff_t>(pos_ + n));
        pos_ += n;
        return true;
    }
    [[nodiscard]] bool take_string(size_t n, std::string& out) {
        if (pos_ + n > payload_.size())
            return false;
        out.assign(reinterpret_cast<const char*>(payload_.data() + pos_), n);
        pos_ += n;
        return true;
    }
    [[nodiscard]] size_t remaining() const noexcept { return payload_.size() - pos_; }
    [[nodiscard]] size_t position() const noexcept { return pos_; }
    [[nodiscard]] std::span<const std::byte> rest() const noexcept {
        return payload_.subspan(pos_);
    }

 private:
    std::span<const std::byte> payload_;
    size_t pos_ = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// Hello: { proto_version u8, max_chunk_size u32, capabilities u32 }
// ---------------------------------------------------------------------------
std::vector<std::byte> encode_hello(const HelloMsg& m) {
    std::vector<std::byte> out;
    out.reserve(1 + 4 + 4);
    append_u8(out, m.protocol_version);
    append_u32(out, m.max_chunk_size);
    append_u32(out, m.capabilities);
    return out;
}

std::optional<HelloMsg> decode_hello(std::span<const std::byte> payload) {
    Cursor c(payload);
    HelloMsg m{};
    if (!c.take_u8(m.protocol_version))
        return std::nullopt;
    if (!c.take_u32(m.max_chunk_size))
        return std::nullopt;
    if (!c.take_u32(m.capabilities))
        return std::nullopt;
    if (c.remaining() != 0)
        return std::nullopt;
    return m;
}

// ---------------------------------------------------------------------------
// Manifest: { file_size u64, chunk_size u32, chunk_count u32, root_hash[32],
//             path_len u16, path[path_len], chunk_hashes[chunk_count*32] }
// ---------------------------------------------------------------------------
std::vector<std::byte> encode_manifest(const ManifestMsg& m) {
    std::vector<std::byte> out;
    out.reserve(8 + 4 + 4 + kHashSize + 2 + m.path.size() + m.chunk_hashes.size() * kHashSize);

    append_u64(out, m.file_size);
    append_u32(out, m.chunk_size);
    append_u32(out, m.chunk_count);
    append_hash(out, m.root_hash);
    append_u16(out, static_cast<uint16_t>(m.path.size()));
    append_bytes(out, std::as_bytes(std::span<const char>(m.path.data(), m.path.size())));
    for (const auto& h : m.chunk_hashes) {
        append_hash(out, h);
    }
    return out;
}

std::optional<ManifestMsg> decode_manifest(std::span<const std::byte> payload) {
    Cursor c(payload);
    ManifestMsg m{};

    if (!c.take_u64(m.file_size))
        return std::nullopt;
    if (!c.take_u32(m.chunk_size))
        return std::nullopt;
    if (!c.take_u32(m.chunk_count))
        return std::nullopt;
    if (!c.take_hash(m.root_hash))
        return std::nullopt;

    uint16_t path_len = 0;
    if (!c.take_u16(path_len))
        return std::nullopt;
    if (!c.take_string(path_len, m.path))
        return std::nullopt;

    m.chunk_hashes.resize(m.chunk_count);
    for (auto& h : m.chunk_hashes) {
        if (!c.take_hash(h))
            return std::nullopt;
    }
    if (c.remaining() != 0)
        return std::nullopt;
    return m;
}

// ---------------------------------------------------------------------------
// ReqChunks: { count u32, indices[count] u32 }
// ---------------------------------------------------------------------------
std::vector<std::byte> encode_req_chunks(const ReqChunksMsg& m) {
    std::vector<std::byte> out;
    out.reserve(4 + m.indices.size() * 4);
    append_u32(out, static_cast<uint32_t>(m.indices.size()));
    for (const auto idx : m.indices) {
        append_u32(out, idx);
    }
    return out;
}

std::optional<ReqChunksMsg> decode_req_chunks(std::span<const std::byte> payload) {
    Cursor c(payload);
    ReqChunksMsg m{};
    uint32_t count = 0;
    if (!c.take_u32(count))
        return std::nullopt;
    if (count > c.remaining() / 4)
        return std::nullopt;  // bound check
    m.indices.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!c.take_u32(m.indices[i]))
            return std::nullopt;
    }
    if (c.remaining() != 0)
        return std::nullopt;
    return m;
}

// ---------------------------------------------------------------------------
// Chunk: { index u32, hash[32], data[remainder] }
// ---------------------------------------------------------------------------
std::vector<std::byte> encode_chunk(const ChunkMsg& m) {
    std::vector<std::byte> out;
    out.reserve(4 + kHashSize + m.data.size());
    append_u32(out, m.index);
    append_hash(out, m.hash);
    append_bytes(out, m.data);
    return out;
}

std::optional<ChunkMsg> decode_chunk(std::span<const std::byte> payload) {
    Cursor c(payload);
    ChunkMsg m{};
    if (!c.take_u32(m.index))
        return std::nullopt;
    if (!c.take_hash(m.hash))
        return std::nullopt;
    const auto rest = c.rest();
    m.data.assign(rest.begin(), rest.end());
    return m;
}

// ---------------------------------------------------------------------------
// Complete: { final_root_hash[32], status u8 }
// ---------------------------------------------------------------------------
std::vector<std::byte> encode_complete(const CompleteMsg& m) {
    std::vector<std::byte> out;
    out.reserve(kHashSize + 1);
    append_hash(out, m.final_root_hash);
    append_u8(out, m.status);
    return out;
}

std::optional<CompleteMsg> decode_complete(std::span<const std::byte> payload) {
    Cursor c(payload);
    CompleteMsg m{};
    if (!c.take_hash(m.final_root_hash))
        return std::nullopt;
    if (!c.take_u8(m.status))
        return std::nullopt;
    if (c.remaining() != 0)
        return std::nullopt;
    return m;
}

// ---------------------------------------------------------------------------
// Ack: { last_index u32 }
// ---------------------------------------------------------------------------
std::vector<std::byte> encode_ack(const AckMsg& m) {
    std::vector<std::byte> out;
    out.reserve(4);
    append_u32(out, m.last_index);
    return out;
}

std::optional<AckMsg> decode_ack(std::span<const std::byte> payload) {
    Cursor c(payload);
    AckMsg m{};
    if (!c.take_u32(m.last_index))
        return std::nullopt;
    if (c.remaining() != 0)
        return std::nullopt;
    return m;
}

// ---------------------------------------------------------------------------
// Error: { code u16, msg_len u16, msg[msg_len] }
// ---------------------------------------------------------------------------
std::vector<std::byte> encode_error(const ErrorMsg& m) {
    std::vector<std::byte> out;
    out.reserve(2 + 2 + m.message.size());
    append_u16(out, static_cast<uint16_t>(m.code));
    append_u16(out, static_cast<uint16_t>(m.message.size()));
    append_bytes(out, std::as_bytes(std::span<const char>(m.message.data(), m.message.size())));
    return out;
}

std::optional<ErrorMsg> decode_error(std::span<const std::byte> payload) {
    Cursor c(payload);
    ErrorMsg m{};

    uint16_t code_raw = 0;
    if (!c.take_u16(code_raw))
        return std::nullopt;
    m.code = static_cast<ErrorCode>(code_raw);

    uint16_t msg_len = 0;
    if (!c.take_u16(msg_len))
        return std::nullopt;
    if (!c.take_string(msg_len, m.message))
        return std::nullopt;
    if (c.remaining() != 0)
        return std::nullopt;
    return m;
}

}  // namespace ftx::proto
