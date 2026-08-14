#include "tcap_core/component.hpp"

namespace tcap_core {

namespace {

Tlv make_integer_tlv(TagClass tag_class,
                     bool constructed,
                     std::uint32_t tag_number,
                     std::int32_t value) {
    Tlv tlv;
    tlv.tag_class = tag_class;
    tlv.constructed = constructed;
    tlv.tag_number = tag_number;
    tlv.value = encode_integer(value);
    return tlv;
}

void encode_operation_code(std::vector<std::uint8_t>& out, const OperationCode& oc) {
    Tlv tlv;
    tlv.tag_class = TagClass::kUniversal;
    tlv.constructed = false;
    if (oc.local.has_value()) {
        tlv.tag_number = UniversalTag::kInteger;
        tlv.value = encode_integer(*oc.local);
    } else if (oc.global.has_value()) {
        tlv.tag_number = UniversalTag::kObjectIdentifier;
        tlv.value = encode_oid(*oc.global);
    }
    encode_tlv(out, tlv);
}

std::optional<OperationCode> decode_operation_code(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kUniversal) {
        return std::nullopt;
    }
    OperationCode oc;
    if (tlv.tag_number == UniversalTag::kInteger) {
        const auto v = decode_integer(tlv.value);
        if (!v.has_value()) {
            return std::nullopt;
        }
        oc.local = *v;
        return oc;
    }
    if (tlv.tag_number == UniversalTag::kObjectIdentifier) {
        const auto v = decode_oid(tlv.value);
        if (!v.has_value()) {
            return std::nullopt;
        }
        oc.global = *v;
        return oc;
    }
    return std::nullopt;
}

} // namespace

Tlv encode_invoke(const Invoke& invoke) {
    std::vector<std::uint8_t> body;
    encode_tlv(
        body,
        make_integer_tlv(TagClass::kUniversal, false, UniversalTag::kInteger, invoke.invoke_id));
    if (invoke.linked_id.has_value()) {
        encode_tlv(body, make_integer_tlv(TagClass::kContext, false, 0, *invoke.linked_id));
    }
    encode_operation_code(body, invoke.operation_code);
    body.insert(body.end(), invoke.parameter.begin(), invoke.parameter.end());

    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = true;
    tlv.tag_number = ComponentTag::kInvoke;
    tlv.value = std::move(body);
    return tlv;
}

Tlv encode_return_result(const ReturnResult& rr, bool is_last) {
    std::vector<std::uint8_t> body;
    encode_tlv(body,
               make_integer_tlv(TagClass::kUniversal, false, UniversalTag::kInteger, rr.invoke_id));
    if (rr.result.has_value()) {
        std::vector<std::uint8_t> seq_body;
        encode_operation_code(seq_body, rr.result->operation_code);
        seq_body.insert(seq_body.end(), rr.result->parameter.begin(), rr.result->parameter.end());

        Tlv seq_tlv;
        seq_tlv.tag_class = TagClass::kUniversal;
        seq_tlv.constructed = true;
        seq_tlv.tag_number = UniversalTag::kSequence;
        seq_tlv.value = std::move(seq_body);
        encode_tlv(body, seq_tlv);
    }

    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = true;
    tlv.tag_number = is_last ? ComponentTag::kReturnResultLast : ComponentTag::kReturnResult;
    tlv.value = std::move(body);
    return tlv;
}

Tlv encode_return_error(const ReturnError& re) {
    std::vector<std::uint8_t> body;
    encode_tlv(body,
               make_integer_tlv(TagClass::kUniversal, false, UniversalTag::kInteger, re.invoke_id));
    encode_operation_code(body, re.error_code);
    body.insert(body.end(), re.parameter.begin(), re.parameter.end());

    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = true;
    tlv.tag_number = ComponentTag::kReturnError;
    tlv.value = std::move(body);
    return tlv;
}

Tlv encode_reject(const Reject& rej) {
    std::vector<std::uint8_t> body;
    if (rej.invoke_id_present) {
        encode_tlv(
            body,
            make_integer_tlv(TagClass::kUniversal, false, UniversalTag::kInteger, rej.invoke_id));
    } else {
        Tlv null_tlv;
        null_tlv.tag_class = TagClass::kUniversal;
        null_tlv.constructed = false;
        null_tlv.tag_number = UniversalTag::kNull;
        encode_tlv(body, null_tlv);
    }

    encode_tlv(
        body,
        make_integer_tlv(TagClass::kContext, false, rej.problem_choice_tag, rej.problem_value));

    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = true;
    tlv.tag_number = ComponentTag::kReject;
    tlv.value = std::move(body);
    return tlv;
}

std::optional<Component> decode_component(const Tlv& tlv) {
    if (tlv.tag_class != TagClass::kContext || !tlv.constructed) {
        return std::nullopt;
    }

    const auto inner = decode_tlvs(tlv.value);
    if (!inner.has_value() || inner->empty()) {
        return std::nullopt;
    }
    const auto& parts = *inner;

    Component out;

    if (tlv.tag_number == ComponentTag::kInvoke) {
        std::size_t idx = 0;
        if (parts[idx].tag_class != TagClass::kUniversal ||
            parts[idx].tag_number != UniversalTag::kInteger) {
            return std::nullopt;
        }
        const auto invoke_id = decode_integer(parts[idx].value);
        if (!invoke_id.has_value()) {
            return std::nullopt;
        }
        ++idx;

        Invoke invoke;
        invoke.invoke_id = *invoke_id;

        if (idx < parts.size() && parts[idx].tag_class == TagClass::kContext &&
            parts[idx].tag_number == 0) {
            const auto lid = decode_integer(parts[idx].value);
            if (!lid.has_value()) {
                return std::nullopt;
            }
            invoke.linked_id = *lid;
            ++idx;
        }

        if (idx >= parts.size()) {
            return std::nullopt;
        }
        const auto oc = decode_operation_code(parts[idx]);
        if (!oc.has_value()) {
            return std::nullopt;
        }
        invoke.operation_code = *oc;
        ++idx;

        if (idx < parts.size()) {
            // Opaque parameter -- re-encode its own real TLV framing verbatim (this project
            // doesn't yet decode MAP/CAP-specific argument types, see this file's own header).
            encode_tlv(invoke.parameter, parts[idx]);
        }

        out.invoke = std::move(invoke);
        return out;
    }

    if (tlv.tag_number == ComponentTag::kReturnResult ||
        tlv.tag_number == ComponentTag::kReturnResultLast) {
        std::size_t idx = 0;
        if (parts[idx].tag_class != TagClass::kUniversal ||
            parts[idx].tag_number != UniversalTag::kInteger) {
            return std::nullopt;
        }
        const auto invoke_id = decode_integer(parts[idx].value);
        if (!invoke_id.has_value()) {
            return std::nullopt;
        }
        ++idx;

        ReturnResult rr;
        rr.invoke_id = *invoke_id;

        if (idx < parts.size()) {
            if (parts[idx].tag_class != TagClass::kUniversal ||
                parts[idx].tag_number != UniversalTag::kSequence) {
                return std::nullopt;
            }
            const auto seq_parts = decode_tlvs(parts[idx].value);
            if (!seq_parts.has_value() || seq_parts->empty()) {
                return std::nullopt;
            }
            const auto oc = decode_operation_code((*seq_parts)[0]);
            if (!oc.has_value()) {
                return std::nullopt;
            }
            ReturnResult::Result result;
            result.operation_code = *oc;
            if (seq_parts->size() > 1) {
                encode_tlv(result.parameter, (*seq_parts)[1]);
            }
            rr.result = std::move(result);
        }

        if (tlv.tag_number == ComponentTag::kReturnResultLast) {
            out.return_result_last = std::move(rr);
        } else {
            out.return_result = std::move(rr);
        }
        return out;
    }

    if (tlv.tag_number == ComponentTag::kReturnError) {
        std::size_t idx = 0;
        if (parts[idx].tag_class != TagClass::kUniversal ||
            parts[idx].tag_number != UniversalTag::kInteger) {
            return std::nullopt;
        }
        const auto invoke_id = decode_integer(parts[idx].value);
        if (!invoke_id.has_value()) {
            return std::nullopt;
        }
        ++idx;

        if (idx >= parts.size()) {
            return std::nullopt;
        }
        const auto ec = decode_operation_code(parts[idx]);
        if (!ec.has_value()) {
            return std::nullopt;
        }
        ++idx;

        ReturnError re;
        re.invoke_id = *invoke_id;
        re.error_code = *ec;
        if (idx < parts.size()) {
            encode_tlv(re.parameter, parts[idx]);
        }

        out.return_error = std::move(re);
        return out;
    }

    if (tlv.tag_number == ComponentTag::kReject) {
        std::size_t idx = 0;
        Reject rej;
        if (parts[idx].tag_class == TagClass::kUniversal &&
            parts[idx].tag_number == UniversalTag::kInteger) {
            const auto invoke_id = decode_integer(parts[idx].value);
            if (!invoke_id.has_value()) {
                return std::nullopt;
            }
            rej.invoke_id_present = true;
            rej.invoke_id = *invoke_id;
        } else if (parts[idx].tag_class == TagClass::kUniversal &&
                   parts[idx].tag_number == UniversalTag::kNull) {
            rej.invoke_id_present = false;
        } else {
            return std::nullopt;
        }
        ++idx;

        if (idx >= parts.size() || parts[idx].tag_class != TagClass::kContext) {
            return std::nullopt;
        }
        const auto problem_value = decode_integer(parts[idx].value);
        if (!problem_value.has_value()) {
            return std::nullopt;
        }
        rej.problem_choice_tag = static_cast<std::uint8_t>(parts[idx].tag_number);
        rej.problem_value = *problem_value;

        out.reject = std::move(rej);
        return out;
    }

    return std::nullopt;
}

} // namespace tcap_core
