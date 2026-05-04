#include "ftx/proto/frame.hpp"

#include <algorithm>
#include <stdexcept>

#include "ftx/util/byteorder.hpp"
#include "ftx/util/crc32c.hpp"

namespace ftx::proto {

namespace {
constexpr size_t kHeaderCrcOffset    = 5;  // bytes [5..9) hold CRC
constexpr size_t kHeaderCrcCoverage  = 5;  // CRC covers bytes [0..5)
}  // namespace

void FrameHeader::encode_to(std::span<std::byte, kHeaderSize> dst) const noexcept {
    write_be_u32(dst.subspan<0, 4>(), payload_len);
    dst[4] = std::byte{static_cast<unsigned char>(type)};
    const auto     covered = std::span<const std::byte>(dst.data(), kHeaderCrcCoverage);
    const uint32_t crc     = crc32c(covered);
    write_be_u32(dst.subspan<kHeaderCrcOffset, 4>(), crc);
}

std::optional<FrameHeader> FrameHeader::decode_from(
    std::span<const std::byte, kHeaderSize> src,
    size_t                                  max_payload,
    DecodeError*                            err_out) noexcept {

    const auto     covered      = std::span<const std::byte>(src.data(), kHeaderCrcCoverage);
    const uint32_t actual_crc   = crc32c(covered);
    const uint32_t expected_crc = read_be_u32(src.subspan<kHeaderCrcOffset, 4>());
    if (expected_crc != actual_crc) {
        if (err_out != nullptr) *err_out = DecodeError::BadCrc;
        return std::nullopt;
    }

    const auto type_byte = std::to_integer<uint8_t>(src[4]);
    if (!is_known_frame_type(type_byte)) {
        if (err_out != nullptr) *err_out = DecodeError::UnknownType;
        return std::nullopt;
    }

    const uint32_t len = read_be_u32(src.subspan<0, 4>());
    if (static_cast<size_t>(len) > max_payload) {
        if (err_out != nullptr) *err_out = DecodeError::PayloadTooLarge;
        return std::nullopt;
    }

    return FrameHeader{
        .payload_len = len,
        .type        = static_cast<FrameType>(type_byte),
    };
}

std::vector<std::byte> encode_frame(FrameType type, std::span<const std::byte> payload) {
    if (payload.size() > kDefaultMaxPayload) {
        throw std::invalid_argument("ftx::proto::encode_frame: payload exceeds maximum");
    }

    std::vector<std::byte> buf(kHeaderSize + payload.size());

    const FrameHeader hdr{
        .payload_len = static_cast<uint32_t>(payload.size()),
        .type        = type,
    };
    hdr.encode_to(std::span<std::byte, kHeaderSize>(buf.data(), kHeaderSize));

    if (!payload.empty()) {
        std::copy(payload.begin(), payload.end(),
                  buf.begin() + static_cast<std::ptrdiff_t>(kHeaderSize));
    }
    return buf;
}

}  // namespace ftx::proto
