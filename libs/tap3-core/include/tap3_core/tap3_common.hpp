#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tcap_core/ber.hpp"

// TAP3 (GSMA TD.57, "TAP 3.12 Format Specification" V36.4, 15 May 2019) roaming CDR codec --
// P4.11's first slice (ADR-0067). Real, cited facts throughout (real ASN.1 tag numbers, real
// clause/section numbers) -- NOT a vendored or reproduced copy of the spec itself. TAP-SPEC.pdf is
// GSMA member-confidential ("Confidential - Full, Rapporteur, Associate and Affiliate Members"),
// so unlike the freely-published 3GPP TS PDFs already committed to this repo, no spec-derived
// file (the PDF itself, or a transcribed .asn grammar) is vendored or committed here -- only real
// facts (field names, real [APPLICATION N] tag numbers, real clause numbers), same citation
// discipline this project already uses for TCAP/MAP/CAP, never large verbatim excerpts.
//
// Real ASN.1 module fact (TAP-SPEC.pdf section 6.1, module header): `TAP-0312 DEFINITIONS
// IMPLICIT TAGS ::= BEGIN`. Real X.680 consequence, already discovered and documented once for
// this exact rule in cap_types.hpp: a CHOICE type cannot be implicitly tagged, so any CHOICE given
// its own `[APPLICATION N]` in the module (e.g. `ChargeableSubscriber ::= [APPLICATION 427]
// CHOICE {...}`) is REAL EXPLICIT tagging on the wire regardless of the module's IMPLICIT default
// -- `wrap_explicit`/`unwrap_explicit` below is the same real pattern cap_types.hpp already
// established, generalized to accept the tag CLASS (TAP3's own tagged CHOICEs use
// TagClass::kApplication, not CAP's TagClass::kContext).
//
// Real ASN.1 module fact: two DIFFERENT CHOICE shapes exist in this module. `DataInterchange` and
// `CallEventDetail` are UNTAGGED CHOICEs (`DataInterchange ::= CHOICE { transferBatch
// TransferBatch, notification Notification, ... }`, no `[APPLICATION N]` prefix on the CHOICE
// itself) -- on the wire, whichever alternative is chosen, THAT alternative's own real tag appears
// directly with no extra wrapper; decode by tag-dispatch, not `unwrap_explicit`.
//
// Real transfer syntax (TAP-SPEC.pdf section 6.2): plain BER (ITU-T X.690), confirmed, not PER --
// this codec reuses tcap_core's own generic X.690 BER primitives (Tlv, encode_tlv/decode_tlv/
// decode_tlvs, encode_integer/decode_integer) directly rather than duplicating them; nothing in
// that particular tcap_core file is TCAP-specific (TCAP's own message/component/dialogue-portion
// framing lives in separate files this codec does NOT depend on).
//
// Real, disclosed scope for this first slice (ADR-0067): the envelope (DataInterchange ->
// TransferBatch/Notification -> BatchControlInfo/AccountingInfo/NetworkInfo/AuditControlInfo) plus
// exactly one real CallEventDetail variant, MobileOriginatedCall (see tap3_mo_call.hpp) -- the
// other ~8 CallEventDetail variants (MobileTerminatedCall, GprsCall, ContentTransaction, etc.) are
// real, cited, NOT implemented this slice, same "one real operation first" precedent MAP's own
// first increment (insertSubscriberData, 1 of ~90 real operations) already set. Charging-detail
// nesting (ChargeInformation/ChargeDetail/TaxInformation/DiscountInformation reached via
// BasicServiceUsed.chargeInformationList and CamelServiceUsed's own tax/discount fields) is
// real, cited, deferred -- charging-detail population belongs with a later stage that actually
// wires live rating decisions into a real outbound TAP3 file, not this "prove the codec is
// real and byte-correct" stage.

namespace tap3_core {

using tcap_core::TagClass;
using tcap_core::Tlv;

// Real [APPLICATION N] tag numbers this slice's structures use, cited from TAP-SPEC.pdf section
// 6.1's own real ASN.1 module text (each constant's own real defining line, e.g.
// `TransferBatch ::= [APPLICATION 1] SEQUENCE`), grouped by the real section of the module they
// come from. NOT exhaustive of the whole module (317 pages) -- only what this slice implements.
namespace Tag {
// Structure of a Tap batch
constexpr std::uint32_t kTransferBatch = 1;
constexpr std::uint32_t kNotification = 2;
constexpr std::uint32_t kCallEventDetailList = 3;
constexpr std::uint32_t kBatchControlInfo = 4;
constexpr std::uint32_t kAccountingInfo = 5;
constexpr std::uint32_t kNetworkInfo = 6;
constexpr std::uint32_t kAuditControlInfo = 15;

// CallEventDetail ::= CHOICE alternatives -- real top tag per variant (TAP-SPEC.pdf section 6.1,
// p.257, each alternative's own real defining line). Untagged CHOICE (see this file's own header
// comment): the wire carries whichever alternative's own tag directly, no extra wrapper.
constexpr std::uint32_t kMobileOriginatedCall = 9;
constexpr std::uint32_t kMobileTerminatedCall = 10;
constexpr std::uint32_t kSupplServiceEvent = 11;
constexpr std::uint32_t kServiceCentreUsage = 12;
constexpr std::uint32_t kGprsCall = 14;
constexpr std::uint32_t kContentTransaction = 17;
constexpr std::uint32_t kLocationService = 297;
constexpr std::uint32_t kMessagingEvent = 433;
constexpr std::uint32_t kMobileSession = 434;
constexpr std::uint32_t kAggregatedUsageRecord = 453;

// Common/tagged data items (alphabetical section of the module)
constexpr std::uint32_t kLocalTimeStamp = 16;
constexpr std::uint32_t kUtcTimeOffset = 231;
constexpr std::uint32_t kUtcTimeOffsetCode = 232;
constexpr std::uint32_t kUtcTimeOffsetInfo = 233;
constexpr std::uint32_t kUtcTimeOffsetInfoList = 234;
constexpr std::uint32_t kSender = 196;
constexpr std::uint32_t kRecipient = 182;
constexpr std::uint32_t kPlmnId = 169;
constexpr std::uint32_t kFileSequenceNumber = 109;
constexpr std::uint32_t kRapFileSequenceNumber = 181;
constexpr std::uint32_t kFileCreationTimeStamp = 108;
constexpr std::uint32_t kTransferCutOffTimeStamp = 227;
constexpr std::uint32_t kFileAvailableTimeStamp = 107;
constexpr std::uint32_t kSpecificationVersionNumber = 201;
constexpr std::uint32_t kReleaseVersionNumber = 189;
constexpr std::uint32_t kFileTypeIndicator = 110;
constexpr std::uint32_t kOperatorSpecInformation = 163;
constexpr std::uint32_t kOperatorSpecInfoList = 162;

// AccountingInfo chain
constexpr std::uint32_t kLocalCurrency = 135;
constexpr std::uint32_t kTapCurrency = 210;
constexpr std::uint32_t kTapDecimalPlaces = 244;
constexpr std::uint32_t kTaxationList = 211;
constexpr std::uint32_t kTaxation = 216;
constexpr std::uint32_t kTaxCode = 212;
constexpr std::uint32_t kTaxType = 217;
constexpr std::uint32_t kTaxRate = 215;
constexpr std::uint32_t kChargeType = 71;
constexpr std::uint32_t kTaxIndicator = 432;
constexpr std::uint32_t kDiscountingList = 95;
constexpr std::uint32_t kDiscounting = 94;
constexpr std::uint32_t kDiscountCode = 91;
constexpr std::uint32_t kDiscountApplied = 428; // CHOICE, real EXPLICIT wrap
constexpr std::uint32_t kFixedDiscountValue = 411;
constexpr std::uint32_t kDiscountRate = 92;
constexpr std::uint32_t kCurrencyConversionList = 80;
constexpr std::uint32_t kCurrencyConversion = 106;
constexpr std::uint32_t kExchangeRateCode = 105;
constexpr std::uint32_t kNumberOfDecimalPlaces = 159;
constexpr std::uint32_t kExchangeRate = 104;

// NetworkInfo chain
constexpr std::uint32_t kRecEntityInfoList = 188;
constexpr std::uint32_t kRecEntityInformation = 183;
constexpr std::uint32_t kRecEntityCode = 184;
constexpr std::uint32_t kRecEntityType = 186;
constexpr std::uint32_t kRecEntityId = 400;

// AuditControlInfo chain
constexpr std::uint32_t kEarliestCallTimeStamp = 101;
constexpr std::uint32_t kLatestCallTimeStamp = 133;
constexpr std::uint32_t kTotalCharge = 415;
constexpr std::uint32_t kTotalChargeRefund = 355;
constexpr std::uint32_t kTotalTaxRefund = 353;
constexpr std::uint32_t kTotalTaxValue = 226;
constexpr std::uint32_t kTotalDiscountValue = 225;
constexpr std::uint32_t kTotalDiscountRefund = 354;
constexpr std::uint32_t kCallEventDetailsCount = 43;
} // namespace Tag

// Real X.680 rule (see this file's own header comment): a CHOICE given its own [APPLICATION N] in
// this module is real EXPLICIT tagging. Same pattern as cap_core::wrap_explicit/unwrap_explicit,
// generalized to accept the real tag class (TAP3's own tagged CHOICEs use TagClass::kApplication).
Tlv wrap_explicit(TagClass tag_class, std::uint32_t tag_number, const Tlv& inner);
std::optional<Tlv>
unwrap_explicit(const Tlv& outer, TagClass tag_class, std::uint32_t expected_tag_number);

// DateTime ::= SEQUENCE { localTimeStamp LocalTimeStamp OPTIONAL, utcTimeOffsetCode
// UtcTimeOffsetCode OPTIONAL } -- real, untagged SEQUENCE (TAP-SPEC.pdf section 6.1, "We start
// with the short datatype referencing the UtcTimeOffsetInfo"). LocalTimeStamp real format
// CCYYMMDDhhmmss (NumberString SIZE(14)) -- stored as the real digit string, not parsed into a
// calendar type (no real use in this codebase needs calendar arithmetic on it yet).
struct DateTime {
    std::optional<std::string> localTimeStamp;
    std::optional<std::int32_t> utcTimeOffsetCode;
};
Tlv encode_date_time(std::uint32_t tag_number, const DateTime& v);
std::optional<DateTime> decode_date_time(const Tlv& tlv, std::uint32_t expected_tag_number);

// DateTimeLong ::= SEQUENCE { localTimeStamp LocalTimeStamp OPTIONAL, utcTimeOffset UtcTimeOffset
// OPTIONAL } -- same real shape as DateTime, but carries the real UTC offset string directly
// (e.g. "+0100") instead of a code reference into UtcTimeOffsetInfoList.
struct DateTimeLong {
    std::optional<std::string> localTimeStamp;
    std::optional<std::string> utcTimeOffset;
};
Tlv encode_date_time_long(std::uint32_t tag_number, const DateTimeLong& v);
std::optional<DateTimeLong> decode_date_time_long(const Tlv& tlv,
                                                  std::uint32_t expected_tag_number);

// Real helpers for the common leaf encodings this whole codec reuses: AsciiString/NumberString/
// HexString/BCDString/Currency all real-alias to `OCTET STRING` (TAP-SPEC.pdf section 6.1, "Tagged
// common data types"/"Non-tagged common data types") -- modeled here as plain std::string (the
// real ISO 646/BCD content), AbsoluteAmount/Code/PercentageRate real-alias to `INTEGER`.
Tlv encode_string_field(TagClass tag_class, std::uint32_t tag_number, const std::string& v);
std::optional<std::string>
decode_string_field(const Tlv& tlv, TagClass tag_class, std::uint32_t expected_tag_number);

Tlv encode_int_field(TagClass tag_class, std::uint32_t tag_number, std::int32_t v);
std::optional<std::int32_t>
decode_int_field(const Tlv& tlv, TagClass tag_class, std::uint32_t expected_tag_number);

// Real, disclosed exception: TAP-SPEC.pdf Table 44 (p.252-253) lists ~20 real fields (e.g. Total
// Charge, Data Volume Incoming/Outgoing, Charging Id, Aggregated Usage Charge) whose real INTEGER
// range needs up to 8 bytes, unlike every other INTEGER in the module (real implicit 4-byte max
// per the spec's own text). Kept as a separate helper (not a change to tcap_core::encode_integer,
// which every other TCAP/MAP/CAP consumer relies on staying 32-bit) -- same minimal-length two's
// complement algorithm (X.690 section 8.3.2), just 8 bytes wide.
Tlv encode_int64_field(TagClass tag_class, std::uint32_t tag_number, std::int64_t v);
std::optional<std::int64_t>
decode_int64_field(const Tlv& tlv, TagClass tag_class, std::uint32_t expected_tag_number);

} // namespace tap3_core
