#include "tcap_core/message.hpp"

namespace tcap_core {

namespace {

void encode_app_octet_string(std::vector<std::uint8_t>& out,
                             std::uint32_t tag_number,
                             const std::vector<std::uint8_t>& value) {
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = false;
    tlv.tag_number = tag_number;
    tlv.value = value;
    encode_tlv(out, tlv);
}

void encode_components(std::vector<std::uint8_t>& out, const std::vector<Tlv>& components) {
    if (components.empty()) {
        return;
    }
    std::vector<std::uint8_t> body;
    for (const auto& c : components) {
        encode_tlv(body, c);
    }
    Tlv wrapper;
    wrapper.tag_class = TagClass::kApplication;
    wrapper.constructed = true;
    wrapper.tag_number = MessageTag::kComponentPortion;
    wrapper.value = std::move(body);
    encode_tlv(out, wrapper);
}

Tlv wrap_application(std::uint32_t tag_number, std::vector<std::uint8_t> body) {
    Tlv tlv;
    tlv.tag_class = TagClass::kApplication;
    tlv.constructed = true;
    tlv.tag_number = tag_number;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<std::vector<Tlv>> decode_body(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    auto outer = decode_tlv(bytes, offset);
    if (!outer.has_value() || outer->tag_class != TagClass::kApplication || !outer->constructed) {
        return std::nullopt;
    }
    return decode_tlvs(outer->value);
}

} // namespace

std::vector<std::uint8_t> encode_tc_begin(const TcBegin& msg) {
    std::vector<std::uint8_t> body;
    encode_app_octet_string(
        body, MessageTag::kOriginatingTransactionId, msg.originating_transaction_id);
    if (msg.dialogue_portion.has_value()) {
        body.insert(body.end(), msg.dialogue_portion->begin(), msg.dialogue_portion->end());
    }
    encode_components(body, msg.components);

    std::vector<std::uint8_t> out;
    encode_tlv(out, wrap_application(MessageTag::kBegin, std::move(body)));
    return out;
}

std::vector<std::uint8_t> encode_tc_continue(const TcContinue& msg) {
    std::vector<std::uint8_t> body;
    encode_app_octet_string(
        body, MessageTag::kOriginatingTransactionId, msg.originating_transaction_id);
    encode_app_octet_string(
        body, MessageTag::kDestinationTransactionId, msg.destination_transaction_id);
    if (msg.dialogue_portion.has_value()) {
        body.insert(body.end(), msg.dialogue_portion->begin(), msg.dialogue_portion->end());
    }
    encode_components(body, msg.components);

    std::vector<std::uint8_t> out;
    encode_tlv(out, wrap_application(MessageTag::kContinue, std::move(body)));
    return out;
}

std::vector<std::uint8_t> encode_tc_end(const TcEnd& msg) {
    std::vector<std::uint8_t> body;
    encode_app_octet_string(
        body, MessageTag::kDestinationTransactionId, msg.destination_transaction_id);
    if (msg.dialogue_portion.has_value()) {
        body.insert(body.end(), msg.dialogue_portion->begin(), msg.dialogue_portion->end());
    }
    encode_components(body, msg.components);

    std::vector<std::uint8_t> out;
    encode_tlv(out, wrap_application(MessageTag::kEnd, std::move(body)));
    return out;
}

std::vector<std::uint8_t> encode_tc_abort(const TcAbort& msg) {
    std::vector<std::uint8_t> body;
    encode_app_octet_string(
        body, MessageTag::kDestinationTransactionId, msg.destination_transaction_id);
    if (msg.p_abort_cause.has_value()) {
        Tlv cause;
        cause.tag_class = TagClass::kApplication;
        cause.constructed = false;
        cause.tag_number = MessageTag::kPAbortCause;
        cause.value = encode_integer(*msg.p_abort_cause);
        encode_tlv(body, cause);
    } else if (msg.dialogue_portion.has_value()) {
        body.insert(body.end(), msg.dialogue_portion->begin(), msg.dialogue_portion->end());
    }

    std::vector<std::uint8_t> out;
    encode_tlv(out, wrap_application(MessageTag::kAbort, std::move(body)));
    return out;
}

std::vector<std::uint8_t> encode_tc_uni(const TcUni& msg) {
    std::vector<std::uint8_t> body;
    if (msg.dialogue_portion.has_value()) {
        body.insert(body.end(), msg.dialogue_portion->begin(), msg.dialogue_portion->end());
    }
    encode_components(body, msg.components);

    std::vector<std::uint8_t> out;
    encode_tlv(out, wrap_application(MessageTag::kUni, std::move(body)));
    return out;
}

std::optional<std::uint32_t> peek_tc_message_tag(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    const auto tlv = decode_tlv(bytes, offset);
    if (!tlv.has_value() || tlv->tag_class != TagClass::kApplication) {
        return std::nullopt;
    }
    return tlv->tag_number;
}

namespace {

// Extracts an optional DialoguePortion (the whole [APPLICATION 11] TLV, re-encoded verbatim --
// dialogue_portion.cpp's own decode functions interpret its content) and the Component list from
// the tail of `parts`, starting at `idx`.
bool decode_dialogue_and_components(const std::vector<Tlv>& parts,
                                    std::size_t idx,
                                    std::optional<std::vector<std::uint8_t>>& dialogue_portion,
                                    std::vector<Tlv>& components) {
    if (idx < parts.size() && parts[idx].tag_class == TagClass::kApplication &&
        parts[idx].tag_number == MessageTag::kDialoguePortion) {
        std::vector<std::uint8_t> dp_bytes;
        encode_tlv(dp_bytes, parts[idx]);
        dialogue_portion = std::move(dp_bytes);
        ++idx;
    }
    if (idx < parts.size() && parts[idx].tag_class == TagClass::kApplication &&
        parts[idx].tag_number == MessageTag::kComponentPortion) {
        const auto comp_tlvs = decode_tlvs(parts[idx].value);
        if (!comp_tlvs.has_value()) {
            return false;
        }
        components = *comp_tlvs;
        ++idx;
    }
    return idx == parts.size();
}

} // namespace

std::optional<TcBegin> decode_tc_begin(const std::vector<std::uint8_t>& bytes) {
    const auto parts = decode_body(bytes);
    if (!parts.has_value() || parts->empty()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    if (p[0].tag_class != TagClass::kApplication ||
        p[0].tag_number != MessageTag::kOriginatingTransactionId) {
        return std::nullopt;
    }

    TcBegin msg;
    msg.originating_transaction_id = p[0].value;
    if (!decode_dialogue_and_components(p, 1, msg.dialogue_portion, msg.components)) {
        return std::nullopt;
    }
    return msg;
}

std::optional<TcContinue> decode_tc_continue(const std::vector<std::uint8_t>& bytes) {
    const auto parts = decode_body(bytes);
    if (!parts.has_value() || parts->size() < 2) {
        return std::nullopt;
    }
    const auto& p = *parts;
    if (p[0].tag_class != TagClass::kApplication ||
        p[0].tag_number != MessageTag::kOriginatingTransactionId ||
        p[1].tag_class != TagClass::kApplication ||
        p[1].tag_number != MessageTag::kDestinationTransactionId) {
        return std::nullopt;
    }

    TcContinue msg;
    msg.originating_transaction_id = p[0].value;
    msg.destination_transaction_id = p[1].value;
    if (!decode_dialogue_and_components(p, 2, msg.dialogue_portion, msg.components)) {
        return std::nullopt;
    }
    return msg;
}

std::optional<TcEnd> decode_tc_end(const std::vector<std::uint8_t>& bytes) {
    const auto parts = decode_body(bytes);
    if (!parts.has_value() || parts->empty()) {
        return std::nullopt;
    }
    const auto& p = *parts;
    if (p[0].tag_class != TagClass::kApplication ||
        p[0].tag_number != MessageTag::kDestinationTransactionId) {
        return std::nullopt;
    }

    TcEnd msg;
    msg.destination_transaction_id = p[0].value;
    if (!decode_dialogue_and_components(p, 1, msg.dialogue_portion, msg.components)) {
        return std::nullopt;
    }
    return msg;
}

std::optional<TcAbort> decode_tc_abort(const std::vector<std::uint8_t>& bytes) {
    const auto parts = decode_body(bytes);
    if (!parts.has_value() || parts->size() < 2) {
        return std::nullopt;
    }
    const auto& p = *parts;
    if (p[0].tag_class != TagClass::kApplication ||
        p[0].tag_number != MessageTag::kDestinationTransactionId) {
        return std::nullopt;
    }

    TcAbort msg;
    msg.destination_transaction_id = p[0].value;
    if (p[1].tag_class == TagClass::kApplication && p[1].tag_number == MessageTag::kPAbortCause) {
        const auto cause = decode_integer(p[1].value);
        if (!cause.has_value()) {
            return std::nullopt;
        }
        msg.p_abort_cause = *cause;
    } else if (p[1].tag_class == TagClass::kApplication &&
               p[1].tag_number == MessageTag::kDialoguePortion) {
        std::vector<std::uint8_t> dp_bytes;
        encode_tlv(dp_bytes, p[1]);
        msg.dialogue_portion = std::move(dp_bytes);
    } else {
        return std::nullopt;
    }
    return msg;
}

std::optional<TcUni> decode_tc_uni(const std::vector<std::uint8_t>& bytes) {
    const auto parts = decode_body(bytes);
    if (!parts.has_value()) {
        return std::nullopt;
    }

    TcUni msg;
    if (!decode_dialogue_and_components(*parts, 0, msg.dialogue_portion, msg.components)) {
        return std::nullopt;
    }
    return msg;
}

} // namespace tcap_core
