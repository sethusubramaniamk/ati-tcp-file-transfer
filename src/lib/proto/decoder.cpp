#include "ftx/proto/decoder.hpp"

namespace ftx::proto {

FrameDecoder::FrameDecoder(size_t max_payload) noexcept : max_payload_(max_payload) {
    buffer_.reserve(kHeaderSize);
}

bool FrameDecoder::feed(std::span<const std::byte> bytes) {
    if (state_ == State::Poisoned)
        return false;
    if (!bytes.empty()) {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    }
    return drive_();
}

bool FrameDecoder::drive_() {
    while (true) {
        switch (state_) {
            case State::Poisoned:
                return false;

            case State::FrameReady:
                // Caller must take_frame() before more progress is made.
                return true;

            case State::ReadingHeader:
                if (buffer_.size() < kHeaderSize) {
                    return true;  // need more bytes
                }
                if (!try_consume_header_()) {
                    return false;  // poisoned
                }
                // try_consume_header_ transitioned us. Loop re-evaluates state.
                continue;

            case State::ReadingPayload:
                if (buffer_.size() < pending_header_.payload_len) {
                    return true;  // need more bytes
                }
                state_ = State::FrameReady;
                return true;
        }
        return true;  // unreachable, silences -Wreturn-type
    }
}

bool FrameDecoder::try_consume_header_() {
    auto hdr_span = std::span<const std::byte, kHeaderSize>(buffer_.data(), kHeaderSize);
    FrameHeader::DecodeError reason{};
    auto hdr = FrameHeader::decode_from(hdr_span, max_payload_, &reason);
    if (!hdr.has_value()) {
        switch (reason) {
            case FrameHeader::DecodeError::BadCrc:
                last_error_ = Error::BadCrc;
                break;
            case FrameHeader::DecodeError::UnknownType:
                last_error_ = Error::UnknownType;
                break;
            case FrameHeader::DecodeError::PayloadTooLarge:
                last_error_ = Error::PayloadTooLarge;
                break;
        }
        state_ = State::Poisoned;
        return false;
    }
    pending_header_ = *hdr;
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize));
    state_ = (pending_header_.payload_len == 0) ? State::FrameReady : State::ReadingPayload;
    return true;
}

bool FrameDecoder::has_frame() const noexcept {
    return state_ == State::FrameReady;
}

Frame FrameDecoder::take_frame() {
    Frame f;
    f.header = pending_header_;
    if (pending_header_.payload_len > 0) {
        const auto take_n = static_cast<std::ptrdiff_t>(pending_header_.payload_len);
        f.payload.assign(buffer_.begin(), buffer_.begin() + take_n);
        buffer_.erase(buffer_.begin(), buffer_.begin() + take_n);
    }
    pending_header_ = {};
    state_ = State::ReadingHeader;
    // Try to advance to the next frame if enough bytes are already buffered.
    (void)drive_();
    return f;
}

std::string_view FrameDecoder::last_error_message() const noexcept {
    switch (last_error_) {
        case Error::None:
            return "";
        case Error::BadCrc:
            return "header CRC mismatch";
        case Error::UnknownType:
            return "unknown frame type";
        case Error::PayloadTooLarge:
            return "payload exceeds maximum";
    }
    return "unknown";
}

}  // namespace ftx::proto
