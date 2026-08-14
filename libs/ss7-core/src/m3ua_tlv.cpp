#include "ss7_core/m3ua_tlv.hpp"

namespace ss7_core {

namespace {

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

std::uint16_t get_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8) |
                                      bytes[offset + 1]);
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

constexpr std::size_t kTlvHeaderLength = 4;

} // namespace

void encode_m3ua_tlv(std::vector<std::uint8_t>& out, const M3uaTlv& tlv) {
    const std::size_t param_len = kTlvHeaderLength + tlv.value.size();

    put_u16(out, tlv.tag);
    put_u16(out, static_cast<std::uint16_t>(param_len));
    out.insert(out.end(), tlv.value.begin(), tlv.value.end());

    const std::size_t pad = padded_length(tlv.value.size()) - tlv.value.size();
    for (std::size_t i = 0; i < pad; ++i) {
        out.push_back(0);
    }
}

std::optional<std::vector<M3uaTlv>> decode_m3ua_tlvs(const std::vector<std::uint8_t>& bytes) {
    std::vector<M3uaTlv> params;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        if (offset + kTlvHeaderLength > bytes.size()) {
            return std::nullopt;
        }

        M3uaTlv tlv;
        tlv.tag = get_u16(bytes, offset);
        const std::uint16_t param_len = get_u16(bytes, offset + 2);

        if (param_len < kTlvHeaderLength || offset + param_len > bytes.size()) {
            return std::nullopt;
        }

        const std::size_t value_len = param_len - kTlvHeaderLength;
        tlv.value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset + kTlvHeaderLength),
                         bytes.begin() +
                             static_cast<std::ptrdiff_t>(offset + kTlvHeaderLength + value_len));

        offset += padded_length(param_len);
        params.push_back(std::move(tlv));
    }

    return params;
}

const M3uaTlv* find_m3ua_tlv(const std::vector<M3uaTlv>& params, std::uint16_t tag) {
    for (const auto& tlv : params) {
        if (tlv.tag == tag) {
            return &tlv;
        }
    }
    return nullptr;
}

std::vector<std::uint8_t> encode_m3ua_uint32(std::uint32_t value) {
    std::vector<std::uint8_t> out;
    put_u32(out, value);
    return out;
}

std::optional<std::uint32_t> decode_m3ua_uint32(const std::vector<std::uint8_t>& data) {
    if (data.size() != 4) {
        return std::nullopt;
    }
    return get_u32(data, 0);
}

} // namespace ss7_core
