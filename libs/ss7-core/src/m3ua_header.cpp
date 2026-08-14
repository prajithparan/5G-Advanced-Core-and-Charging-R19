#include "ss7_core/m3ua_header.hpp"

namespace ss7_core {

namespace {

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

std::uint32_t get_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

constexpr std::size_t kHeaderLength = 8;

} // namespace

std::vector<std::uint8_t> encode_m3ua_header(const M3uaHeader& header,
                                             std::uint32_t payload_length) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderLength);

    out.push_back(kM3uaVersion);
    out.push_back(0); // Reserved -- RFC 4666 §3.1: "SHOULD be set to all '0's and ignored"
    out.push_back(header.message_class);
    out.push_back(header.message_type);
    put_u32(out, static_cast<std::uint32_t>(kHeaderLength) + payload_length);

    return out;
}

std::optional<M3uaHeader> decode_m3ua_header(const std::vector<std::uint8_t>& bytes,
                                             std::size_t& offset,
                                             std::uint32_t& payload_length) {
    if (bytes.size() < offset + kHeaderLength) {
        return std::nullopt;
    }

    const std::size_t start = offset;
    if (bytes[start] != kM3uaVersion) {
        return std::nullopt;
    }

    const std::uint32_t message_length = get_u32(bytes, start + 4);
    if (message_length < kHeaderLength) {
        return std::nullopt;
    }

    M3uaHeader header;
    header.message_class = bytes[start + 2];
    header.message_type = bytes[start + 3];

    payload_length = message_length - static_cast<std::uint32_t>(kHeaderLength);
    offset = start + kHeaderLength;
    return header;
}

} // namespace ss7_core
