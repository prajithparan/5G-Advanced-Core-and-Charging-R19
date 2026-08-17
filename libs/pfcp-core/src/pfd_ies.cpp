#include "pfcp_core/pfd_ies.hpp"

namespace pfcp_core {

namespace {
constexpr std::uint8_t kFlagFd = 0x01;
constexpr std::uint8_t kFlagUrl = 0x02;
constexpr std::uint8_t kFlagDn = 0x04;
constexpr std::uint8_t kFlagCp = 0x08;

void append_length_prefixed(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& v) {
    const auto length = static_cast<std::uint16_t>(v.size());
    out.push_back(static_cast<std::uint8_t>(length >> 8));
    out.push_back(static_cast<std::uint8_t>(length & 0xFF));
    out.insert(out.end(), v.begin(), v.end());
}

// Returns std::nullopt if `bytes` doesn't have a full 2-byte length prefix plus that many
// following bytes at `offset`; advances `offset` past the field on success.
std::optional<std::vector<std::uint8_t>>
read_length_prefixed(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    if (offset + 2 > bytes.size()) {
        return std::nullopt;
    }
    const std::uint16_t length = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8) | bytes[offset + 1]);
    offset += 2;
    if (offset + length > bytes.size()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> v(bytes.begin() + static_cast<long>(offset),
                                bytes.begin() + static_cast<long>(offset + length));
    offset += length;
    return v;
}
} // namespace

std::vector<std::uint8_t> encode_pfd_contents(const PfdContents& contents) {
    std::uint8_t flags = 0;
    if (contents.flow_description.has_value()) {
        flags |= kFlagFd;
    }
    if (contents.url.has_value()) {
        flags |= kFlagUrl;
    }
    if (contents.domain_name.has_value()) {
        flags |= kFlagDn;
    }
    if (contents.custom_content.has_value()) {
        flags |= kFlagCp;
    }

    std::vector<std::uint8_t> out;
    out.push_back(flags);
    out.push_back(0x00); // octet 6, spare per Figure 8.2.39-1

    // Field order per Figure 8.2.39-1: Flow Description, URL, Domain Name, Custom PFD Content --
    // each only present if its own flag bit is set.
    if (contents.flow_description.has_value()) {
        append_length_prefixed(out, *contents.flow_description);
    }
    if (contents.url.has_value()) {
        append_length_prefixed(out, *contents.url);
    }
    if (contents.domain_name.has_value()) {
        append_length_prefixed(out, *contents.domain_name);
    }
    if (contents.custom_content.has_value()) {
        append_length_prefixed(out, *contents.custom_content);
    }
    return out;
}

std::optional<PfdContents> decode_pfd_contents(const std::vector<std::uint8_t>& value) {
    if (value.size() < 2) {
        return std::nullopt;
    }
    const std::uint8_t flags = value[0];
    // value[1] is the spare octet 6 -- intentionally unread.
    std::size_t offset = 2;

    PfdContents contents;
    if ((flags & kFlagFd) != 0) {
        auto v = read_length_prefixed(value, offset);
        if (!v.has_value()) {
            return std::nullopt;
        }
        contents.flow_description = std::move(*v);
    }
    if ((flags & kFlagUrl) != 0) {
        auto v = read_length_prefixed(value, offset);
        if (!v.has_value()) {
            return std::nullopt;
        }
        contents.url = std::move(*v);
    }
    if ((flags & kFlagDn) != 0) {
        auto v = read_length_prefixed(value, offset);
        if (!v.has_value()) {
            return std::nullopt;
        }
        contents.domain_name = std::move(*v);
    }
    if ((flags & kFlagCp) != 0) {
        auto v = read_length_prefixed(value, offset);
        if (!v.has_value()) {
            return std::nullopt;
        }
        contents.custom_content = std::move(*v);
    }
    return contents;
}

} // namespace pfcp_core
