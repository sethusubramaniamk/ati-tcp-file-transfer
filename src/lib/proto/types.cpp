#include "ftx/proto/types.hpp"

namespace ftx::proto {

bool is_known_frame_type(uint8_t v) noexcept {
    switch (v) {
        case static_cast<uint8_t>(FrameType::Hello):
        case static_cast<uint8_t>(FrameType::Manifest):
        case static_cast<uint8_t>(FrameType::ReqChunks):
        case static_cast<uint8_t>(FrameType::Chunk):
        case static_cast<uint8_t>(FrameType::Ack):
        case static_cast<uint8_t>(FrameType::Complete):
        case static_cast<uint8_t>(FrameType::Error):
            return true;
        default:
            return false;
    }
}

std::string_view to_string(FrameType t) noexcept {
    switch (t) {
        case FrameType::Hello:     return "HELLO";
        case FrameType::Manifest:  return "MANIFEST";
        case FrameType::ReqChunks: return "REQ_CHUNKS";
        case FrameType::Chunk:     return "CHUNK";
        case FrameType::Ack:       return "ACK";
        case FrameType::Complete:  return "COMPLETE";
        case FrameType::Error:     return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace ftx::proto
