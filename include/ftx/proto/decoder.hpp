#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "ftx/proto/frame.hpp"

namespace ftx::proto {

// Streaming frame decoder. Caller feeds bytes (in any chunking) and pulls
// completed frames as they become available. After a protocol violation the
// decoder is poisoned and must be discarded.
//
// Memory bound: at most kHeaderSize + max_payload bytes are buffered.
class FrameDecoder {
 public:
    enum class Error {
        None,
        BadCrc,
        UnknownType,
        PayloadTooLarge,
    };

    explicit FrameDecoder(size_t max_payload = kDefaultMaxPayload) noexcept;

    // Append received bytes. Returns false on protocol violation; the decoder
    // is then poisoned and last_error() / last_error_message() describe the
    // cause. Subsequent feeds also return false.
    [[nodiscard]] bool feed(std::span<const std::byte> bytes);

    // True iff a fully-parsed frame is available via take_frame().
    [[nodiscard]] bool has_frame() const noexcept;

    // Pull and return the available frame. Precondition: has_frame() == true.
    // After the call the decoder advances; if more buffered bytes are enough
    // for the next frame, has_frame() may be true again immediately.
    [[nodiscard]] Frame take_frame();

    [[nodiscard]] Error            last_error() const noexcept { return last_error_; }
    [[nodiscard]] std::string_view last_error_message() const noexcept;
    [[nodiscard]] bool             poisoned() const noexcept { return last_error_ != Error::None; }

 private:
    enum class State { ReadingHeader, ReadingPayload, FrameReady, Poisoned };

    bool drive_();
    bool try_consume_header_();

    size_t                 max_payload_;
    State                  state_      = State::ReadingHeader;
    Error                  last_error_ = Error::None;
    std::vector<std::byte> buffer_;
    FrameHeader            pending_header_{};
};

}  // namespace ftx::proto
