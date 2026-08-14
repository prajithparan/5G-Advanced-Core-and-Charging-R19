#include "ss7_core/m3ua_asp.hpp"

#include "ss7_core/m3ua_dictionary.hpp"
#include "ss7_core/m3ua_tlv.hpp"

namespace ss7_core {

namespace {

std::vector<std::uint8_t> encode_message(std::uint8_t message_class,
                                         std::uint8_t message_type,
                                         const std::vector<std::uint8_t>& payload) {
    M3uaHeader header;
    header.message_class = message_class;
    header.message_type = message_type;
    auto out = encode_m3ua_header(header, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::optional<std::vector<M3uaTlv>> decode_message_payload(std::uint8_t expected_class,
                                                           std::uint8_t expected_type,
                                                           const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    const auto header = decode_m3ua_header(bytes, offset, payload_length);
    if (!header.has_value() || header->message_class != expected_class ||
        header->message_type != expected_type) {
        return std::nullopt;
    }
    if (offset + payload_length != bytes.size()) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                            bytes.end());
    return decode_m3ua_tlvs(payload);
}

} // namespace

std::vector<std::uint8_t> encode_asp_state_message(std::uint8_t message_type,
                                                   const AspStateMessage& msg) {
    std::vector<std::uint8_t> payload;
    if (msg.asp_identifier.has_value()) {
        M3uaTlv tlv;
        tlv.tag = dictionary::ParamTag::kAspIdentifier;
        tlv.value = encode_m3ua_uint32(*msg.asp_identifier);
        encode_m3ua_tlv(payload, tlv);
    }
    if (msg.info_string.has_value()) {
        M3uaTlv tlv;
        tlv.tag = dictionary::ParamTag::kInfoString;
        tlv.value.assign(msg.info_string->begin(), msg.info_string->end());
        encode_m3ua_tlv(payload, tlv);
    }
    return encode_message(dictionary::MessageClass::kAspsm, message_type, payload);
}

std::optional<AspStateMessage> decode_asp_state_message(std::uint8_t expected_message_type,
                                                        const std::vector<std::uint8_t>& bytes) {
    const auto tlvs =
        decode_message_payload(dictionary::MessageClass::kAspsm, expected_message_type, bytes);
    if (!tlvs.has_value()) {
        return std::nullopt;
    }

    AspStateMessage msg;
    if (const auto* id_tlv = find_m3ua_tlv(*tlvs, dictionary::ParamTag::kAspIdentifier);
        id_tlv != nullptr) {
        const auto v = decode_m3ua_uint32(id_tlv->value);
        if (!v.has_value()) {
            return std::nullopt;
        }
        msg.asp_identifier = *v;
    }
    if (const auto* info_tlv = find_m3ua_tlv(*tlvs, dictionary::ParamTag::kInfoString);
        info_tlv != nullptr) {
        msg.info_string = std::string(info_tlv->value.begin(), info_tlv->value.end());
    }
    return msg;
}

std::vector<std::uint8_t> encode_asp_traffic_message(std::uint8_t message_type,
                                                     const AspTrafficMessage& msg) {
    std::vector<std::uint8_t> payload;
    if (msg.traffic_mode_type.has_value()) {
        M3uaTlv tlv;
        tlv.tag = dictionary::ParamTag::kTrafficModeType;
        tlv.value = encode_m3ua_uint32(*msg.traffic_mode_type);
        encode_m3ua_tlv(payload, tlv);
    }
    if (msg.routing_context.has_value()) {
        M3uaTlv tlv;
        tlv.tag = dictionary::ParamTag::kRoutingContext;
        for (const auto v : *msg.routing_context) {
            const auto encoded = encode_m3ua_uint32(v);
            tlv.value.insert(tlv.value.end(), encoded.begin(), encoded.end());
        }
        encode_m3ua_tlv(payload, tlv);
    }
    if (msg.info_string.has_value()) {
        M3uaTlv tlv;
        tlv.tag = dictionary::ParamTag::kInfoString;
        tlv.value.assign(msg.info_string->begin(), msg.info_string->end());
        encode_m3ua_tlv(payload, tlv);
    }
    return encode_message(dictionary::MessageClass::kAsptm, message_type, payload);
}

std::optional<AspTrafficMessage>
decode_asp_traffic_message(std::uint8_t expected_message_type,
                           const std::vector<std::uint8_t>& bytes) {
    const auto tlvs =
        decode_message_payload(dictionary::MessageClass::kAsptm, expected_message_type, bytes);
    if (!tlvs.has_value()) {
        return std::nullopt;
    }

    AspTrafficMessage msg;
    if (const auto* tmt_tlv = find_m3ua_tlv(*tlvs, dictionary::ParamTag::kTrafficModeType);
        tmt_tlv != nullptr) {
        const auto v = decode_m3ua_uint32(tmt_tlv->value);
        if (!v.has_value()) {
            return std::nullopt;
        }
        msg.traffic_mode_type = *v;
    }
    if (const auto* rc_tlv = find_m3ua_tlv(*tlvs, dictionary::ParamTag::kRoutingContext);
        rc_tlv != nullptr) {
        if (rc_tlv->value.size() % 4 != 0) {
            return std::nullopt;
        }
        std::vector<std::uint32_t> contexts;
        for (std::size_t i = 0; i < rc_tlv->value.size(); i += 4) {
            const std::vector<std::uint8_t> chunk(
                rc_tlv->value.begin() + static_cast<std::ptrdiff_t>(i),
                rc_tlv->value.begin() + static_cast<std::ptrdiff_t>(i + 4));
            const auto v = decode_m3ua_uint32(chunk);
            if (!v.has_value()) {
                return std::nullopt;
            }
            contexts.push_back(*v);
        }
        msg.routing_context = std::move(contexts);
    }
    if (const auto* info_tlv = find_m3ua_tlv(*tlvs, dictionary::ParamTag::kInfoString);
        info_tlv != nullptr) {
        msg.info_string = std::string(info_tlv->value.begin(), info_tlv->value.end());
    }
    return msg;
}

} // namespace ss7_core
