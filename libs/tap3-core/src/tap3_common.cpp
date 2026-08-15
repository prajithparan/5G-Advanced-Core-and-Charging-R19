#include "tap3_core/tap3_common.hpp"

namespace tap3_core {

Tlv wrap_explicit(TagClass tag_class, std::uint32_t tag_number, const Tlv& inner) {
    Tlv outer;
    outer.tag_class = tag_class;
    outer.constructed = true;
    outer.tag_number = tag_number;
    encode_tlv(outer.value, inner);
    return outer;
}

std::optional<Tlv>
unwrap_explicit(const Tlv& outer, TagClass tag_class, std::uint32_t expected_tag_number) {
    if (outer.tag_class != tag_class || !outer.constructed ||
        outer.tag_number != expected_tag_number) {
        return std::nullopt;
    }
    std::size_t offset = 0;
    return tcap_core::decode_tlv(outer.value, offset);
}

Tlv encode_string_field(TagClass tag_class, std::uint32_t tag_number, const std::string& v) {
    Tlv tlv;
    tlv.tag_class = tag_class;
    tlv.constructed = false;
    tlv.tag_number = tag_number;
    tlv.value.assign(v.begin(), v.end());
    return tlv;
}

std::optional<std::string>
decode_string_field(const Tlv& tlv, TagClass tag_class, std::uint32_t expected_tag_number) {
    if (tlv.tag_class != tag_class || tlv.tag_number != expected_tag_number) {
        return std::nullopt;
    }
    return std::string(tlv.value.begin(), tlv.value.end());
}

Tlv encode_int_field(TagClass tag_class, std::uint32_t tag_number, std::int32_t v) {
    Tlv tlv;
    tlv.tag_class = tag_class;
    tlv.constructed = false;
    tlv.tag_number = tag_number;
    tlv.value = tcap_core::encode_integer(v);
    return tlv;
}

std::optional<std::int32_t>
decode_int_field(const Tlv& tlv, TagClass tag_class, std::uint32_t expected_tag_number) {
    if (tlv.tag_class != tag_class || tlv.tag_number != expected_tag_number) {
        return std::nullopt;
    }
    return tcap_core::decode_integer(tlv.value);
}

namespace {

// Same minimal-length two's complement algorithm as tcap_core::encode_integer (X.690 section
// 8.3.2), widened to 8 bytes for this module's own real 8-byte-INTEGER exception fields.
std::vector<std::uint8_t> encode_integer64(std::int64_t value) {
    std::uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) {
        bytes[i] =
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) >> (8 * (7 - i))) & 0xFF);
    }
    std::size_t start = 0;
    while (start < 7 && ((bytes[start] == 0x00 && (bytes[start + 1] & 0x80) == 0) ||
                         (bytes[start] == 0xFF && (bytes[start + 1] & 0x80) != 0))) {
        ++start;
    }
    return std::vector<std::uint8_t>(bytes + start, bytes + 8);
}

std::optional<std::int64_t> decode_integer64(const std::vector<std::uint8_t>& data) {
    if (data.empty() || data.size() > 8) {
        return std::nullopt;
    }
    std::int64_t value = (data[0] & 0x80) ? -1 : 0;
    for (const auto b : data) {
        value = (value << 8) | b;
    }
    return value;
}

} // namespace

Tlv encode_int64_field(TagClass tag_class, std::uint32_t tag_number, std::int64_t v) {
    Tlv tlv;
    tlv.tag_class = tag_class;
    tlv.constructed = false;
    tlv.tag_number = tag_number;
    tlv.value = encode_integer64(v);
    return tlv;
}

std::optional<std::int64_t>
decode_int64_field(const Tlv& tlv, TagClass tag_class, std::uint32_t expected_tag_number) {
    if (tlv.tag_class != tag_class || tlv.tag_number != expected_tag_number) {
        return std::nullopt;
    }
    return decode_integer64(tlv.value);
}

namespace {

// DateTime/DateTimeLong are real, untagged SEQUENCEs (TAP-SPEC.pdf section 6.1) -- the caller
// supplies the real enclosing tag number (LocalTimeStamp/UtcTimeOffsetCode/UtcTimeOffset each
// carry their OWN real tag as members, same "every SEQUENCE member is independently tagged"
// pattern this whole module uses throughout).
constexpr std::uint32_t kLocalTimeStampTag = Tag::kLocalTimeStamp;
constexpr std::uint32_t kUtcTimeOffsetCodeTag = Tag::kUtcTimeOffsetCode;
constexpr std::uint32_t kUtcTimeOffsetTag = Tag::kUtcTimeOffset;

} // namespace

Tlv encode_date_time(std::uint32_t tag_number, const DateTime& v) {
    std::vector<std::uint8_t> body;
    if (v.localTimeStamp.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, kLocalTimeStampTag, *v.localTimeStamp));
    }
    if (v.utcTimeOffsetCode.has_value()) {
        encode_tlv(
            body,
            encode_int_field(TagClass::kApplication, kUtcTimeOffsetCodeTag, *v.utcTimeOffsetCode));
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = tag_number;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<DateTime> decode_date_time(const Tlv& tlv, std::uint32_t expected_tag_number) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != expected_tag_number) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    DateTime out;
    std::size_t idx = 0;
    const auto& p = *parts;
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == kLocalTimeStampTag) {
        out.localTimeStamp =
            decode_string_field(p[idx], TagClass::kApplication, kLocalTimeStampTag);
        ++idx;
    }
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == kUtcTimeOffsetCodeTag) {
        out.utcTimeOffsetCode =
            decode_int_field(p[idx], TagClass::kApplication, kUtcTimeOffsetCodeTag);
        ++idx;
    }
    return out;
}

Tlv encode_date_time_long(std::uint32_t tag_number, const DateTimeLong& v) {
    std::vector<std::uint8_t> body;
    if (v.localTimeStamp.has_value()) {
        encode_tlv(
            body,
            encode_string_field(TagClass::kApplication, kLocalTimeStampTag, *v.localTimeStamp));
    }
    if (v.utcTimeOffset.has_value()) {
        encode_tlv(
            body, encode_string_field(TagClass::kApplication, kUtcTimeOffsetTag, *v.utcTimeOffset));
    }
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = tag_number;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<DateTimeLong> decode_date_time_long(const Tlv& tlv,
                                                  std::uint32_t expected_tag_number) {
    if (tlv.tag_class != TagClass::kApplication || tlv.tag_number != expected_tag_number) {
        return std::nullopt;
    }
    const auto parts = tcap_core::decode_tlvs(tlv.value);
    if (!parts.has_value()) {
        return std::nullopt;
    }
    DateTimeLong out;
    std::size_t idx = 0;
    const auto& p = *parts;
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == kLocalTimeStampTag) {
        out.localTimeStamp =
            decode_string_field(p[idx], TagClass::kApplication, kLocalTimeStampTag);
        ++idx;
    }
    if (idx < p.size() && p[idx].tag_class == TagClass::kApplication &&
        p[idx].tag_number == kUtcTimeOffsetTag) {
        out.utcTimeOffset = decode_string_field(p[idx], TagClass::kApplication, kUtcTimeOffsetTag);
        ++idx;
    }
    return out;
}

} // namespace tap3_core
