#include "pfcp_core/header.hpp"

namespace pfcp_core {

namespace {
constexpr std::size_t kNodeHeaderOverhead = 4;    // sequence number(3) + spare(1)
constexpr std::size_t kSessionHeaderOverhead = 12; // SEID(8) + sequence number(3) + priority/spare(1)
} // namespace

std::vector<std::uint8_t> encode_header(const Header& header, std::uint16_t ies_length) {
    std::vector<std::uint8_t> out;
    const std::size_t overhead = header.has_seid ? kSessionHeaderOverhead : kNodeHeaderOverhead;
    const std::uint16_t message_length = static_cast<std::uint16_t>(overhead + ies_length);

    // Octet 1: bits 8-6 version, bits 5-3 spare(0), bit 2 MP(0, not used by this build), bit 1 S.
    out.push_back(static_cast<std::uint8_t>((kPfcpVersion << 5) | (header.has_seid ? 0x01 : 0x00)));
    out.push_back(static_cast<std::uint8_t>(header.message_type));
    out.push_back(static_cast<std::uint8_t>(message_length >> 8));
    out.push_back(static_cast<std::uint8_t>(message_length & 0xFF));

    if (header.has_seid) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>((header.seid >> shift) & 0xFF));
        }
    }

    out.push_back(static_cast<std::uint8_t>((header.sequence_number >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((header.sequence_number >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(header.sequence_number & 0xFF));
    out.push_back(0x00); // spare octet (node-related) / message priority+spare (session-related)

    return out;
}

std::optional<Header> decode_header(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                                    std::uint16_t& ies_length) {
    if (bytes.size() < offset + 4) {
        return std::nullopt;
    }
    const std::uint8_t octet1 = bytes[offset];
    const std::uint8_t version = static_cast<std::uint8_t>(octet1 >> 5);
    if (version != kPfcpVersion) {
        return std::nullopt;
    }
    const bool has_seid = (octet1 & 0x01) != 0;
    const std::size_t overhead = has_seid ? kSessionHeaderOverhead : kNodeHeaderOverhead;

    Header header;
    header.has_seid = has_seid;
    header.message_type = static_cast<MessageType>(bytes[offset + 1]);
    const std::uint16_t message_length =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset + 2]) << 8) |
                                   bytes[offset + 3]);
    if (message_length < overhead) {
        return std::nullopt;
    }
    if (bytes.size() < offset + 4 + overhead) {
        return std::nullopt;
    }

    std::size_t pos = offset + 4;
    if (has_seid) {
        std::uint64_t seid = 0;
        for (int i = 0; i < 8; ++i) {
            seid = (seid << 8) | bytes[pos + static_cast<std::size_t>(i)];
        }
        header.seid = seid;
        pos += 8;
    }

    header.sequence_number = (static_cast<std::uint32_t>(bytes[pos]) << 16) |
                             (static_cast<std::uint32_t>(bytes[pos + 1]) << 8) |
                             static_cast<std::uint32_t>(bytes[pos + 2]);
    pos += 4; // sequence number(3) + spare/priority octet(1)

    ies_length = static_cast<std::uint16_t>(message_length - overhead);
    offset = pos;
    return header;
}

} // namespace pfcp_core
