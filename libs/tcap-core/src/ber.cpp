#include "tcap_core/ber.hpp"

namespace tcap_core {

namespace {

void encode_tag(std::vector<std::uint8_t>& out,
                TagClass tag_class,
                bool constructed,
                std::uint32_t tag_number) {
    std::uint8_t first = static_cast<std::uint8_t>((static_cast<std::uint8_t>(tag_class) << 6) |
                                                   (constructed ? 0x20 : 0x00));
    if (tag_number < 31) {
        first |= static_cast<std::uint8_t>(tag_number);
        out.push_back(first);
        return;
    }

    first |= 0x1F;
    out.push_back(first);

    // High-tag-number form (X.690 §8.1.2.4): base-128, most-significant byte first, continuation
    // bit set on every byte except the last.
    std::vector<std::uint8_t> arc_bytes;
    std::uint32_t v = tag_number;
    do {
        arc_bytes.push_back(static_cast<std::uint8_t>(v & 0x7F));
        v >>= 7;
    } while (v != 0);
    for (std::size_t i = arc_bytes.size(); i-- > 0;) {
        std::uint8_t b = arc_bytes[i];
        if (i != 0) {
            b |= 0x80;
        }
        out.push_back(b);
    }
}

void encode_length(std::vector<std::uint8_t>& out, std::size_t len) {
    if (len < 128) {
        out.push_back(static_cast<std::uint8_t>(len));
        return;
    }
    std::vector<std::uint8_t> len_bytes;
    std::size_t v = len;
    while (v != 0) {
        len_bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
        v >>= 8;
    }
    out.push_back(static_cast<std::uint8_t>(0x80 | len_bytes.size()));
    for (std::size_t i = len_bytes.size(); i-- > 0;) {
        out.push_back(len_bytes[i]);
    }
}

} // namespace

void encode_tlv(std::vector<std::uint8_t>& out, const Tlv& tlv) {
    encode_tag(out, tlv.tag_class, tlv.constructed, tlv.tag_number);
    encode_length(out, tlv.value.size());
    out.insert(out.end(), tlv.value.begin(), tlv.value.end());
}

std::optional<Tlv> decode_tlv(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
    if (offset >= bytes.size()) {
        return std::nullopt;
    }

    std::size_t pos = offset;
    const std::uint8_t first = bytes[pos++];

    Tlv tlv;
    tlv.tag_class = static_cast<TagClass>((first >> 6) & 0x03);
    tlv.constructed = (first & 0x20) != 0;

    std::uint32_t tag_number = first & 0x1F;
    if (tag_number == 0x1F) {
        tag_number = 0;
        bool last_seen = false;
        while (pos < bytes.size()) {
            const std::uint8_t b = bytes[pos++];
            tag_number = (tag_number << 7) | (b & 0x7F);
            if ((b & 0x80) == 0) {
                last_seen = true;
                break;
            }
        }
        if (!last_seen) {
            return std::nullopt;
        }
    }
    tlv.tag_number = tag_number;

    if (pos >= bytes.size()) {
        return std::nullopt;
    }
    const std::uint8_t len_first = bytes[pos++];
    std::size_t length = 0;
    if ((len_first & 0x80) == 0) {
        length = len_first;
    } else {
        const std::size_t num_len_bytes = len_first & 0x7F;
        if (num_len_bytes == 0 || pos + num_len_bytes > bytes.size()) {
            return std::nullopt; // indefinite length (0x80) or malformed -- not supported
        }
        for (std::size_t i = 0; i < num_len_bytes; ++i) {
            length = (length << 8) | bytes[pos++];
        }
    }

    if (pos + length > bytes.size()) {
        return std::nullopt;
    }
    tlv.value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                     bytes.begin() + static_cast<std::ptrdiff_t>(pos + length));
    offset = pos + length;
    return tlv;
}

std::optional<std::vector<Tlv>> decode_tlvs(const std::vector<std::uint8_t>& bytes) {
    std::vector<Tlv> tlvs;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto tlv = decode_tlv(bytes, offset);
        if (!tlv.has_value()) {
            return std::nullopt;
        }
        tlvs.push_back(std::move(*tlv));
    }
    return tlvs;
}

std::vector<std::uint8_t> encode_oid(const std::vector<std::uint32_t>& arcs) {
    std::vector<std::uint8_t> out;
    if (arcs.size() < 2) {
        return out; // malformed input -- real OIDs always have at least 2 arcs
    }

    out.push_back(static_cast<std::uint8_t>(arcs[0] * 40 + arcs[1]));
    for (std::size_t i = 2; i < arcs.size(); ++i) {
        std::uint32_t v = arcs[i];
        std::vector<std::uint8_t> arc_bytes;
        do {
            arc_bytes.push_back(static_cast<std::uint8_t>(v & 0x7F));
            v >>= 7;
        } while (v != 0);
        for (std::size_t j = arc_bytes.size(); j-- > 0;) {
            std::uint8_t b = arc_bytes[j];
            if (j != 0) {
                b |= 0x80;
            }
            out.push_back(b);
        }
    }
    return out;
}

std::optional<std::vector<std::uint32_t>> decode_oid(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return std::nullopt;
    }

    std::vector<std::uint32_t> arcs;
    arcs.push_back(data[0] / 40);
    arcs.push_back(data[0] % 40);

    std::uint32_t current = 0;
    bool in_progress = false;
    for (std::size_t i = 1; i < data.size(); ++i) {
        const std::uint8_t b = data[i];
        current = (current << 7) | (b & 0x7F);
        in_progress = true;
        if ((b & 0x80) == 0) {
            arcs.push_back(current);
            current = 0;
            in_progress = false;
        }
    }
    if (in_progress) {
        return std::nullopt; // truncated final arc
    }
    return arcs;
}

std::vector<std::uint8_t> encode_integer(std::int32_t value) {
    std::vector<std::uint8_t> out;
    // Minimal-length two's complement (X.690 §8.3.2): start with all 4 bytes, then drop
    // leading bytes that are pure sign-extension (0x00 followed by a non-negative next byte, or
    // 0xFF followed by a negative next byte).
    std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) >> 24) & 0xFF),
        static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) >> 16) & 0xFF),
        static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) >> 8) & 0xFF),
        static_cast<std::uint8_t>(static_cast<std::uint32_t>(value) & 0xFF),
    };
    std::size_t start = 0;
    while (start < 3 && ((bytes[start] == 0x00 && (bytes[start + 1] & 0x80) == 0) ||
                         (bytes[start] == 0xFF && (bytes[start + 1] & 0x80) != 0))) {
        ++start;
    }
    for (std::size_t i = start; i < 4; ++i) {
        out.push_back(bytes[i]);
    }
    return out;
}

std::optional<std::int32_t> decode_integer(const std::vector<std::uint8_t>& data) {
    if (data.empty() || data.size() > 4) {
        return std::nullopt;
    }
    std::int32_t value = (data[0] & 0x80) ? -1 : 0; // sign-extend
    for (const auto b : data) {
        value = (value << 8) | b;
    }
    return value;
}

} // namespace tcap_core
