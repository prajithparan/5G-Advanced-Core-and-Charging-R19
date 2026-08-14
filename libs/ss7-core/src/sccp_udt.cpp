#include "ss7_core/sccp_udt.hpp"

#include "ss7_core/sccp_dictionary.hpp"

namespace ss7_core {

namespace {

constexpr std::size_t kFixedPartLength = 5; // type(1) + proto_class(1) + 3 pointers(1 each)

void append_length_prefixed(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& v) {
    out.push_back(static_cast<std::uint8_t>(v.size()));
    out.insert(out.end(), v.begin(), v.end());
}

} // namespace

std::vector<std::uint8_t> encode_sccp_udt(const SccpUdt& udt) {
    const auto called_bytes = encode_sccp_address(udt.called_party);
    const auto calling_bytes = encode_sccp_address(udt.calling_party);

    // Pointer value = offset (in octets) from the pointer's OWN position to the first octet (the
    // length octet) of the field it points to -- see this file's own header for the real,
    // disclosed evidence-tier caveat on this convention.
    const std::uint8_t pointer1 =
        static_cast<std::uint8_t>(kFixedPartLength - 2); // from index 2 to index 5
    const std::size_t called_field_len = 1 + called_bytes.size();
    const std::uint8_t pointer2 = static_cast<std::uint8_t>(
        (kFixedPartLength + called_field_len) - 3); // from index 3 to start of calling field
    const std::size_t calling_field_len = 1 + calling_bytes.size();
    const std::uint8_t pointer3 =
        static_cast<std::uint8_t>((kFixedPartLength + called_field_len + calling_field_len) -
                                  4); // from index 4 to start of data field

    std::vector<std::uint8_t> out;
    out.reserve(kFixedPartLength + called_field_len + calling_field_len + 1 + udt.data.size());

    out.push_back(dictionary::MessageType::kUdt);
    out.push_back(udt.protocol_class);
    out.push_back(pointer1);
    out.push_back(pointer2);
    out.push_back(pointer3);

    append_length_prefixed(out, called_bytes);
    append_length_prefixed(out, calling_bytes);
    append_length_prefixed(out, udt.data);

    return out;
}

std::optional<SccpUdt> decode_sccp_udt(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kFixedPartLength) {
        return std::nullopt;
    }
    if (bytes[0] != dictionary::MessageType::kUdt) {
        return std::nullopt;
    }

    SccpUdt udt;
    udt.protocol_class = bytes[1];

    const std::uint8_t pointer1 = bytes[2];
    const std::uint8_t pointer2 = bytes[3];
    const std::uint8_t pointer3 = bytes[4];

    const std::size_t called_len_pos = 2 + pointer1;
    if (called_len_pos >= bytes.size()) {
        return std::nullopt;
    }
    const std::size_t called_len = bytes[called_len_pos];
    if (called_len_pos + 1 + called_len > bytes.size()) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> called_bytes(
        bytes.begin() + static_cast<std::ptrdiff_t>(called_len_pos + 1),
        bytes.begin() + static_cast<std::ptrdiff_t>(called_len_pos + 1 + called_len));
    auto called_party = decode_sccp_address(called_bytes);
    if (!called_party.has_value()) {
        return std::nullopt;
    }
    udt.called_party = *called_party;

    const std::size_t calling_len_pos = 3 + pointer2;
    if (calling_len_pos >= bytes.size()) {
        return std::nullopt;
    }
    const std::size_t calling_len = bytes[calling_len_pos];
    if (calling_len_pos + 1 + calling_len > bytes.size()) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> calling_bytes(
        bytes.begin() + static_cast<std::ptrdiff_t>(calling_len_pos + 1),
        bytes.begin() + static_cast<std::ptrdiff_t>(calling_len_pos + 1 + calling_len));
    auto calling_party = decode_sccp_address(calling_bytes);
    if (!calling_party.has_value()) {
        return std::nullopt;
    }
    udt.calling_party = *calling_party;

    const std::size_t data_len_pos = 4 + pointer3;
    if (data_len_pos >= bytes.size()) {
        return std::nullopt;
    }
    const std::size_t data_len = bytes[data_len_pos];
    if (data_len_pos + 1 + data_len > bytes.size()) {
        return std::nullopt;
    }
    udt.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(data_len_pos + 1),
                    bytes.begin() + static_cast<std::ptrdiff_t>(data_len_pos + 1 + data_len));

    return udt;
}

} // namespace ss7_core
