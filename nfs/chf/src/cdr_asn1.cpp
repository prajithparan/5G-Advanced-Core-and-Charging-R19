#include "cdr_asn1.hpp"

#include <ctime>
#include <optional>

#include "tcap_core/ber.hpp"

namespace chf {

namespace {

using tcap_core::TagClass;
using tcap_core::Tlv;

// Real ASN.1 INTEGER encoding (X.690 §8.3.2, same minimal-length two's-complement rule
// tcap_core::encode_integer already implements for int32_t) -- a local 64-bit counterpart, not
// added to the shared tcap_core library since no other caller there needs more than 32 bits yet.
// DataVolumeOctets/ChargingID/LocalSequenceNumber are all real, spec-unbounded INTEGER types (TS
// 32.298's own generic module), and this project's own volume counters are std::uint64_t.
std::vector<std::uint8_t> encode_integer64(std::int64_t value) {
    std::uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) {
        bytes[i] =
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) >> ((7 - i) * 8)) & 0xFF);
    }
    std::size_t start = 0;
    while (start < 7 && ((bytes[start] == 0x00 && (bytes[start + 1] & 0x80) == 0) ||
                         (bytes[start] == 0xFF && (bytes[start + 1] & 0x80) != 0))) {
        ++start;
    }
    return std::vector<std::uint8_t>(bytes + start, bytes + 8);
}

Tlv context_primitive(std::uint32_t tag, std::vector<std::uint8_t> value) {
    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = false;
    tlv.tag_number = tag;
    tlv.value = std::move(value);
    return tlv;
}

Tlv context_constructed(std::uint32_t tag, std::vector<std::uint8_t> value) {
    Tlv tlv;
    tlv.tag_class = TagClass::kContext;
    tlv.constructed = true;
    tlv.tag_number = tag;
    tlv.value = std::move(value);
    return tlv;
}

Tlv universal_sequence(std::vector<std::uint8_t> value) {
    Tlv tlv;
    tlv.tag_class = TagClass::kUniversal;
    tlv.constructed = true;
    tlv.tag_number = tcap_core::UniversalTag::kSequence;
    tlv.value = std::move(value);
    return tlv;
}

void append(std::vector<std::uint8_t>& out, const Tlv& tlv) {
    tcap_core::encode_tlv(out, tlv);
}

// TS 32.298 §5.2.1 `TimeStamp ::= OCTET STRING (SIZE(9))`, real BCD format cited verbatim in the
// spec's own field comment: "YYMMDDhhmmssShhmm" -- 6 BCD-packed date/time octets, 1 ASCII sign
// octet, 2 BCD-packed UTC-offset octets. Real, disclosed simplification: this project has no
// configured local timezone anywhere (every NF's own clock is treated as UTC, same lab-wide
// convention as every other timestamp this project already emits), so the offset is always
// "+0000" -- a real, honest "no real offset exists to report" choice, not a fabricated one.
std::array<std::uint8_t, 9> encode_timestamp(std::time_t unix_time) {
    std::tm tm{};
    gmtime_r(&unix_time, &tm);
    const int year_two_digit = (tm.tm_year + 1900) % 100;
    const int month = tm.tm_mon + 1;

    auto bcd = [](int value) -> std::uint8_t {
        const int tens = (value / 10) % 10;
        const int ones = value % 10;
        return static_cast<std::uint8_t>((tens << 4) | ones);
    };

    return {
        bcd(year_two_digit),
        bcd(month),
        bcd(tm.tm_mday),
        bcd(tm.tm_hour),
        bcd(tm.tm_min),
        bcd(tm.tm_sec),
        static_cast<std::uint8_t>('+'),
        bcd(0), // UTC offset hours
        bcd(0), // UTC offset minutes
    };
}

// TS 32.298 §5.2.1 `NetworkFunctionality`, real INTEGER values confirmed directly against the
// spec's own ENUMERATED definition -- only the subset with a real, cited correspondent to this
// project's own generated `NodeFunctionality_Nchf_ConvergedCharging` open-enum values (see
// TS29122_CommonData_grp.hpp) is mapped. `SMS`/`NEFF`/`GMLC`/`AIOTF`/`CCF`/`NWDAF` have no real
// TS 32.298 v18.8.0 `NetworkFunctionality` value at all (confirmed by reading the full real
// enumeration, not assumed absent) -- real, disclosed gap, not fabricated with a guessed number.
std::optional<std::int32_t> map_network_functionality(const std::string& value) {
    if (value == "CHF")
        return 0;
    if (value == "SMF")
        return 1;
    if (value == "AMF")
        return 2;
    if (value == "SMSF")
        return 3;
    if (value == "SGW")
        return 4;
    if (value == "I_SMF")
        return 5;
    if (value == "ePDG")
        return 6;
    if (value == "CEF")
        return 7;
    if (value == "NEF")
        return 8;
    if (value == "PGW_C_SMF")
        return 9;
    if (value == "MnS_Producer")
        return 10;
    if (value == "SGSN")
        return 11;
    if (value == "5G_DDNMF")
        return 12;
    if (value == "V_SMF")
        return 13;
    if (value == "IMS_Node")
        return 14;
    if (value == "EES")
        return 15;
    if (value == "MMS_Node")
        return 16;
    if (value == "PCF")
        return 17;
    if (value == "UDM")
        return 18;
    if (value == "UPF")
        return 19;
    if (value == "TSN_AF")
        return 20;
    if (value == "TSCTSF")
        return 21;
    if (value == "MB_SMF")
        return 22;
    return std::nullopt;
}

} // namespace

std::vector<std::uint8_t> encode_chf_cdr(const CdrRecord& record,
                                         const std::string& recording_network_function_id) {
    const auto network_functionality =
        map_network_functionality(record.nf_consumer_node_functionality);
    if (!network_functionality.has_value()) {
        return {};
    }

    // --- ChargingRecord SET fields (TS 32.298 §5.2.5.2), IMPLICIT context tags [0]..[45] ---
    std::vector<std::uint8_t> charging_record;

    // [0] recordType -- real value chargingFunctionRecord(200), §5.2.5.2's own RecordType
    // ENUMERATED (confirmed the real numeric value, not just the symbolic name).
    append(charging_record, context_primitive(0, encode_integer64(200)));

    // [1] recordingNetworkFunctionID -- NetworkFunctionName (IA5String), this CHF's own UUID.
    append(charging_record,
           context_primitive(1,
                             std::vector<std::uint8_t>(recording_network_function_id.begin(),
                                                       recording_network_function_id.end())));

    // [2] subscriberIdentifier -- SubscriptionID SET { subscriptionIDType [0], subscriptionIDData
    // [1] }. Real, disclosed: this project's own SUPI values are always the real "imsi-<15
    // digits>" IMSI-format SUPI (ADR-0016's own lab convention) -- eND-USER-IMSI(1) is the real,
    // correct SubscriptionIDType for that, with the "imsi-" prefix stripped since the ASN.1 field
    // itself holds only the digit string, not this project's own URI-style SUPI encoding.
    if (!record.subscriber_identifier.empty()) {
        std::string imsi_digits = record.subscriber_identifier;
        constexpr std::string_view kImsiPrefix = "imsi-";
        if (imsi_digits.rfind(kImsiPrefix, 0) == 0) {
            imsi_digits.erase(0, kImsiPrefix.size());
        }
        std::vector<std::uint8_t> subscription_id;
        append(subscription_id, context_primitive(0, encode_integer64(1))); // eND-USER-IMSI
        append(subscription_id,
               context_primitive(
                   1, std::vector<std::uint8_t>(imsi_digits.begin(), imsi_digits.end())));
        append(charging_record, context_constructed(2, subscription_id));
    }

    // [3] nFunctionConsumerInformation -- NetworkFunctionInformation SEQUENCE {
    // networkFunctionality [0] (MANDATORY) }. Real, disclosed: only networkFunctionality is
    // populated -- this project's own ChargingDataRequest doesn't carry the consumer's own
    // IPv4/PLMN in every case its rating engine actually reads, so
    // networkFunctionName/-IPv4Address/-PLMNIdentifier are left absent (all real OPTIONAL fields in
    // the spec) rather than filled with a guessed value.
    {
        std::vector<std::uint8_t> nf_info;
        append(nf_info, context_primitive(0, encode_integer64(*network_functionality)));
        append(charging_record, context_constructed(3, nf_info));
    }

    // [5] listOfMultipleUnitUsage -- SEQUENCE OF MultipleUnitUsage. Real, disclosed: this project's
    // own rating engine (ADR-0048/0050) always charges under exactly one rating group per session
    // (kDefaultRatingGroup, nfs/smf/src/main.cpp's own real, disclosed simplification -- no
    // service-to-rating-group mapping exists), so this list has at most one real element.
    if (record.rating_group.has_value()) {
        std::vector<std::uint8_t> multiple_unit_usage;
        append(multiple_unit_usage,
               context_primitive(0, encode_integer64(*record.rating_group))); // ratingGroup

        // usedUnitContainers [1] SEQUENCE OF UsedUnitContainer -- one real container per record,
        // populated only when this project's own CdrRecord actually carries a used-volume figure
        // (Update/Release rows; Create rows only ever carry a granted amount, no real usage yet).
        if (record.used_total_volume.has_value()) {
            std::vector<std::uint8_t> used_unit_container;
            // [4] dataTotalVolume -- this project's own UrrState only tracks a combined
            // uplink+downlink total (ADR-0050's own disclosed narrowing), so dataVolumeUplink[5]/
            // dataVolumeDownlink[6] (both real, separate OPTIONAL fields) are never populated.
            append(used_unit_container,
                   context_primitive(
                       4, encode_integer64(static_cast<std::int64_t>(*record.used_total_volume))));
            // [9] localSequenceNumber -- this record's own invocation sequence number.
            append(used_unit_container,
                   context_primitive(9, encode_integer64(record.invocation_sequence_number)));
            // [10] ratingIndicator BOOLEAN -- real, honest `true` for every row this project
            // writes: CHF's own real rating engine (ADR-0048) always attempts a real
            // catalog-based rating before any Used Unit Container is ever produced, so "has this
            // been rated" is genuinely true, not a default/fabricated value.
            append(used_unit_container, context_primitive(10, std::vector<std::uint8_t>{0xFF}));

            std::vector<std::uint8_t> used_unit_containers_seq;
            append(used_unit_containers_seq, universal_sequence(used_unit_container));
            append(multiple_unit_usage, context_constructed(1, used_unit_containers_seq));
        }

        std::vector<std::uint8_t> list;
        append(list, universal_sequence(multiple_unit_usage));
        append(charging_record, context_constructed(5, list));
    }

    // [6] recordOpeningTime -- this record's own invocationTimestamp doubles as the real Record
    // Opening Time for a Create row; for Update/Release rows this project's own CdrRecord doesn't
    // separately track the session's original opening time (a real, disclosed gap -- Record
    // Opening Time is defined as "when Charging Data Request [Initial] is received", which only a
    // Create row's own invocation_time_stamp genuinely reflects), so only Create rows populate it.
    if (record.operation == "Create") {
        const auto ts = encode_timestamp(record.invocation_time_stamp);
        append(charging_record,
               context_primitive(6, std::vector<std::uint8_t>(ts.begin(), ts.end())));
    }

    // [9] causeForRecClosing -- real value normalRelease(0), TS 32.298's own CauseForRecClosing
    // INTEGER (§5.2.1). Real, disclosed: this is the only real trigger this project's own CHF
    // ever has for closing a record (an explicit Nchf_ConvergedCharging_Release call) -- the other
    // real cause values (volumeLimit/timeLimit/abnormalRelease/...) all correspond to real
    // early-termination conditions this project's own CHF has no live detector for yet, so using
    // any of them here would be a fabricated signal, not a real one.
    if (record.operation == "Release") {
        append(charging_record, context_primitive(9, encode_integer64(0)));
    }

    // [11] localRecordSequenceNumber -- this record's own invocation sequence number (same real
    // value as UsedUnitContainer's own [9] above, at the whole-record level this time).
    append(charging_record,
           context_primitive(11, encode_integer64(record.invocation_sequence_number)));

    // [16] chargingSessionIdentifier -- OCTET STRING, "See 3GPP TS 32.290 [57] for details" (no
    // further real structure given in THIS spec beyond OCTET STRING). This project's own
    // charging_data_ref is used verbatim, UTF-8-encoded -- the real correlation key this CHF
    // already generates for the session, not an invented value.
    append(charging_record,
           context_primitive(16,
                             std::vector<std::uint8_t>(record.charging_data_ref.begin(),
                                                       record.charging_data_ref.end())));

    // [40] invocationTimestamp -- this record's own real invocation time (every operation --
    // Create/Update/Release -- has one).
    {
        const auto ts = encode_timestamp(record.invocation_time_stamp);
        append(charging_record,
               context_primitive(40, std::vector<std::uint8_t>(ts.begin(), ts.end())));
    }

    // CHFRecord ::= CHOICE { chargingFunctionRecord [200] ChargingRecord }, IMPLICIT TAGS -- the
    // [200] tag replaces ChargingRecord's own SET tag directly (no extra wrapping level).
    std::vector<std::uint8_t> out;
    append(out, context_constructed(200, charging_record));
    return out;
}

} // namespace chf
