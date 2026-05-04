#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ftx::io {

// Persistent receiver-side resume state. Stored alongside the .partial file:
//   <dest>.ftxstate
//
// Binary layout (little-endian for u32, big-endian doesn't matter — only this
// program reads it):
//   0..3   magic   "FTXS"
//   4      version u8
//   5..36  manifest_id  32 bytes (BLAKE3 of the encoded manifest payload)
//   37..40 chunk_count  u32 LE
//   41..   bitmap (ceil(chunk_count/8) bytes; bit n indicates chunk n is on disk)
class ResumeState {
 public:
    using ManifestId = std::array<std::byte, 32>;

    ResumeState() = default;
    ResumeState(ManifestId id, uint32_t chunk_count);

    [[nodiscard]] uint32_t          chunk_count()       const noexcept { return chunk_count_; }
    [[nodiscard]] const ManifestId& manifest_id()       const noexcept { return manifest_id_; }
    [[nodiscard]] bool              is_set(uint32_t i)  const noexcept;

    void mark_received(uint32_t i) noexcept;

    // Returns the indices of chunks that are not yet received.
    [[nodiscard]] std::vector<uint32_t> missing() const;

    // True iff every chunk in [0, chunk_count) is marked received.
    [[nodiscard]] bool complete() const noexcept;

    // Persist to disk. Best-effort write to a temp + rename for atomicity.
    [[nodiscard]] bool save(const std::filesystem::path& path) const;

    // Load a previously-written state file. Returns false if the file is
    // missing, malformed, or version-mismatched.
    [[nodiscard]] static bool load(const std::filesystem::path& path, ResumeState& out);

    // Best-effort removal — used after successful finalize.
    static void remove(const std::filesystem::path& path) noexcept;

 private:
    ManifestId            manifest_id_{};
    uint32_t              chunk_count_ = 0;
    std::vector<uint8_t>  bitmap_;
};

}  // namespace ftx::io
