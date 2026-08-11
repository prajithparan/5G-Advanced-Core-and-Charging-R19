#include "diameter_core/avp.hpp"

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

std::size_t padded_length(std::size_t len) {
    return (len + 3) & ~static_cast<std::size_t>(3);
}

} // namespace

void encode_avp(std::vector<std::uint8_t>& out, const Avp& avp) {
    const bool has_vendor = (avp.flags & AvpFlag::kVendor) != 0;
    const std::size_t header_len = has_vendor ? 12 : 8;
    const std::size_t avp_len = header_len + avp.data.size();

    put_u32(out, avp.code);
    out.push_back(avp.flags);
    put_u24(out, static_cast<std::uint32_t>(avp_len));
    if (has_vendor) {
        put_u32(out, avp.vendor_id);
    }
    out.insert(out.end(), avp.data.begin(), avp.data.end());

    const std::size_t pad = padded_length(avp.data.size()) - avp.data.size();
    for (std::size_t i = 0; i < pad; ++i) {
        out.push_back(0);
    }
}

std::vector<std::uint8_t> encode_octet_string(const std::string& value) {
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

std::vector<std::uint8_t> encode_unsigned32(std::uint32_t value) {
    std::vector<std::uint8_t> out;
    put_u32(out, value);
    return out;
}

std::vector<std::uint8_t> encode_integer32(std::int32_t value) {
    return encode_unsigned32(static_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> encode_unsigned64(std::uint64_t value) {
    std::vector<std::uint8_t> out;
    put_u32(out, static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFULL));
    put_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    return out;
}

std::optional<std::string> decode_octet_string(const std::vector<std::uint8_t>& data) {
    return std::string(data.begin(), data.end());
}

std::optional<std::uint32_t> decode_unsigned32(const std::vector<std::uint8_t>& data) {
    if (data.size() != 4) {
        return std::nullopt;
    }
    return get_u32(data, 0);
}

std::optional<std::int32_t> decode_integer32(const std::vector<std::uint8_t>& data) {
    auto u = decode_unsigned32(data);
    if (!u.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*u);
}

std::optional<std::uint64_t> decode_unsigned64(const std::vector<std::uint8_t>& data) {
    if (data.size() != 8) {
        return std::nullopt;
    }
    const std::uint64_t hi = get_u32(data, 0);
    const std::uint64_t lo = get_u32(data, 4);
    return (hi << 32) | lo;
}

std::vector<std::uint8_t> encode_address_ipv4(std::uint32_t ipv4_host_order) {
    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>((kAddressFamilyIpv4 >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(kAddressFamilyIpv4 & 0xFF));
    put_u32(out, ipv4_host_order);
    return out;
}

std::optional<std::uint32_t> decode_address_ipv4(const std::vector<std::uint8_t>& data) {
    if (data.size() != 6) {
        return std::nullopt;
    }
    const std::uint16_t family = (static_cast<std::uint16_t>(data[0]) << 8) | data[1];
    if (family != kAddressFamilyIpv4) {
        return std::nullopt;
    }
    return get_u32(data, 2);
}

std::optional<std::vector<Avp>> decode_avps(const std::vector<std::uint8_t>& bytes) {
    std::vector<Avp> avps;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        if (offset + 8 > bytes.size()) {
            return std::nullopt;
        }

        Avp avp;
        avp.code = get_u32(bytes, offset);
        avp.flags = bytes[offset + 4];
        const std::uint32_t avp_len = get_u24(bytes, offset + 5);

        const bool has_vendor = (avp.flags & AvpFlag::kVendor) != 0;
        const std::size_t header_len = has_vendor ? 12 : 8;
        if (avp_len < header_len || offset + avp_len > bytes.size()) {
            return std::nullopt;
        }

        std::size_t pos = offset + 8;
        if (has_vendor) {
            avp.vendor_id = get_u32(bytes, pos);
            pos += 4;
        }

        const std::size_t data_len = avp_len - header_len;
        avp.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                        bytes.begin() + static_cast<std::ptrdiff_t>(pos + data_len));

        offset += padded_length(avp_len);
        avps.push_back(std::move(avp));
    }

    return avps;
}

const Avp* find_avp(const std::vector<Avp>& avps, std::uint32_t code, std::uint32_t vendor_id) {
    for (const auto& avp : avps) {
        if (avp.code != code) {
            continue;
        }
        if (vendor_id != 0) {
            const bool has_vendor = (avp.flags & AvpFlag::kVendor) != 0;
            if (!has_vendor || avp.vendor_id != vendor_id) {
                continue;
            }
        }
        return &avp;
    }
    return nullptr;
}

} // namespace diameter_core
