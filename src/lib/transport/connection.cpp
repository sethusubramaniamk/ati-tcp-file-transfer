#include "ftx/transport/connection.hpp"

#include <array>
#include <vector>

namespace ftx::transport {

Connection::Connection(asio::ip::tcp::socket socket) noexcept
    : socket_(std::move(socket)) {}

Connection::~Connection() { close(); }

Connection::Connection(Connection&&) noexcept            = default;
Connection& Connection::operator=(Connection&&) noexcept = default;

bool Connection::send_frame(proto::FrameType type, std::span<const std::byte> payload) {
    if (payload.size() > proto::kDefaultMaxPayload) {
        last_error_ = "send_frame: payload exceeds maximum";
        return false;
    }

    std::array<std::byte, proto::kHeaderSize> header_buf{};
    const proto::FrameHeader hdr{
        .payload_len = static_cast<uint32_t>(payload.size()),
        .type        = type,
    };
    hdr.encode_to(header_buf);

    const std::array<asio::const_buffer, 2> bufs{
        asio::buffer(header_buf.data(), header_buf.size()),
        asio::buffer(payload.data(),    payload.size()),
    };

    asio::error_code ec;
    asio::write(socket_, bufs, ec);
    if (ec) {
        last_error_ = "send_frame: " + ec.message();
        return false;
    }
    return true;
}

std::optional<proto::Frame> Connection::recv_frame(size_t max_payload) {
    std::array<std::byte, proto::kHeaderSize> header_buf{};
    asio::error_code ec;
    asio::read(socket_, asio::buffer(header_buf.data(), header_buf.size()), ec);
    if (ec) {
        last_error_ = "recv_frame header: " + ec.message();
        return std::nullopt;
    }

    proto::FrameHeader::DecodeError reason{};
    auto hdr = proto::FrameHeader::decode_from(header_buf, max_payload, &reason);
    if (!hdr.has_value()) {
        switch (reason) {
            case proto::FrameHeader::DecodeError::BadCrc:
                last_error_ = "recv_frame: header CRC mismatch"; break;
            case proto::FrameHeader::DecodeError::UnknownType:
                last_error_ = "recv_frame: unknown frame type"; break;
            case proto::FrameHeader::DecodeError::PayloadTooLarge:
                last_error_ = "recv_frame: payload too large"; break;
        }
        return std::nullopt;
    }

    proto::Frame f;
    f.header = *hdr;
    if (hdr->payload_len > 0) {
        f.payload.resize(hdr->payload_len);
        asio::read(socket_, asio::buffer(f.payload.data(), f.payload.size()), ec);
        if (ec) {
            last_error_ = "recv_frame payload: " + ec.message();
            return std::nullopt;
        }
    }
    return f;
}

void Connection::close() noexcept {
    if (socket_.is_open()) {
        asio::error_code ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);  // ignore failure
        socket_.close(ec);
    }
}

bool Connection::is_open() const noexcept {
    return socket_.is_open();
}

}  // namespace ftx::transport
