#include "cap_core/cap_types.hpp"

namespace cap_core {

using tcap_core::decode_integer;
using tcap_core::decode_tlv;
using tcap_core::encode_integer;
using tcap_core::encode_tlv;
using tcap_core::TagClass;

Tlv wrap_explicit(std::uint32_t tag_number, const Tlv& inner) {
    Tlv outer;
    outer.tag_class = TagClass::kContext;
    outer.constructed = true;
    outer.tag_number = tag_number;
    encode_tlv(outer.value, inner);
    return outer;
}

std::optional<Tlv> unwrap_explicit(const Tlv& outer, std::uint32_t expected_tag_number) {
    if (outer.tag_class != TagClass::kContext || !outer.constructed ||
        outer.tag_number != expected_tag_number) {
        return std::nullopt;
    }
    std::size_t offset = 0;
    return decode_tlv(outer.value, offset);
}

Tlv encode_sending_side_id(LegType leg) {
    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = false;
    tlv.tag_number = 0;
    tlv.value = {static_cast<std::uint8_t>(leg)};
    return tlv;
}

std::optional<LegType> decode_sending_side_id(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kContext || tlv.constructed || tlv.tag_number != 0 ||
        tlv.value.size() != 1) {
        return std::nullopt;
    }
    if (tlv.value[0] != static_cast<std::uint8_t>(LegType::kLeg1) &&
        tlv.value[0] != static_cast<std::uint8_t>(LegType::kLeg2)) {
        return std::nullopt;
    }
    return static_cast<LegType>(tlv.value[0]);
}

Tlv encode_receiving_side_id(LegType leg) {
    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = false;
    tlv.tag_number = 1;
    tlv.value = {static_cast<std::uint8_t>(leg)};
    return tlv;
}

std::optional<LegType> decode_receiving_side_id(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kContext || tlv.constructed || tlv.tag_number != 1 ||
        tlv.value.size() != 1) {
        return std::nullopt;
    }
    if (tlv.value[0] != static_cast<std::uint8_t>(LegType::kLeg1) &&
        tlv.value[0] != static_cast<std::uint8_t>(LegType::kLeg2)) {
        return std::nullopt;
    }
    return static_cast<LegType>(tlv.value[0]);
}

Tlv encode_time_information_no_tariff_switch(std::int32_t hundred_ms_units) {
    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = false;
    tlv.tag_number = 0;
    tlv.value = encode_integer(hundred_ms_units);
    return tlv;
}

std::optional<std::int32_t> decode_time_information_no_tariff_switch(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kContext || tlv.constructed || tlv.tag_number != 0) {
        return std::nullopt;
    }
    return decode_integer(tlv.value);
}

std::vector<std::uint8_t> encode_cause(const std::vector<std::uint8_t>& cause_octets) {
    return cause_octets;
}

std::vector<std::uint8_t> decode_cause(const Tlv& tlv) {
    return tlv.value;
}

} // namespace cap_core
