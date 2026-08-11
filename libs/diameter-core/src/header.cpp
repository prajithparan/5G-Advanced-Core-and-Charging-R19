#include "diameter_core/header.hpp"

namespace diameter_core {

namespace {

void put_u24(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

std::uint32_t get_u24(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 2]);
}

std::uint32_t get_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

constexpr std::size_t kHeaderLength = 20;

} // namespace

std::vector<std::uint8_t> encode_header(const Header& header, std::uint32_t avps_length) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderLength);

    out.push_back(kDiameterVersion);
    put_u24(out, static_cast<std::uint32_t>(kHeaderLength) + avps_length);

    out.push_back(header.flags);
    put_u24(out, header.command_code);

    put_u32(out, header.application_id);
    put_u32(out, header.hop_by_hop_id);
    put_u32(out, header.end_to_end_id);

    return out;
}

std::optional<Header> decode_header(const std::vector<std::uint8_t>& bytes,
                                    std::size_t& offset,
                                    std::uint32_t& avps_length) {
    if (bytes.size() < offset + kHeaderLength) {
        return std::nullopt;
    }

    const std::size_t start = offset;
    if (bytes[start] != kDiameterVersion) {
        return std::nullopt;
    }

    const std::uint32_t message_length = get_u24(bytes, start + 1);
    if (message_length < kHeaderLength) {
        return std::nullopt;
    }

    Header header;
    header.flags = bytes[start + 4];
    header.command_code = get_u24(bytes, start + 5);
    header.application_id = get_u32(bytes, start + 8);
    header.hop_by_hop_id = get_u32(bytes, start + 12);
    header.end_to_end_id = get_u32(bytes, start + 16);

    avps_length = message_length - static_cast<std::uint32_t>(kHeaderLength);
    offset = start + kHeaderLength;
    return header;
}

} // namespace diameter_core
