// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #108, ADR-0089): real TS 32.298 CHF-CDR BER
// encoding (nfs/chf/src/cdr_asn1.hpp/.cpp). Compiled directly against the same
// nfs/chf/src/cdr_asn1.cpp translation unit CHF itself builds -- cdr_asn1.hpp/.cpp only depends
// on cdr.hpp (its own sibling, same NF) and libs/tcap-core's public BER primitives, so this is a
// clean, standalone compilation unit, same precedent as test_ai_inference.cpp's own header
// comment already established for exactly this class of NF-private-code unit test.
//
// Real, structural verification (same class as PfcpPfdIes.ApplicationIdsPfdsGroupRoundTripsVia...
// and every other grouped-ASN.1-structure test this project already has): decodes the encoded
// bytes back via tcap_core::decode_tlv/decode_tlvs and asserts the real tag numbers/classes/values
// match what was set, rather than re-deriving expected bytes by hand (which would just duplicate
// the encoder's own logic).

#include "cdr_asn1.hpp"
#include "tcap_core/ber.hpp"

#include <gtest/gtest.h>

namespace {

using tcap_core::TagClass;

// Finds the first top-level TLV with the given context tag number in `tlvs`.
const tcap_core::Tlv* find_context(const std::vector<tcap_core::Tlv>& tlvs, std::uint32_t tag) {
    for (const auto& tlv : tlvs) {
        if (tlv.tag_class == TagClass::kContext && tlv.tag_number == tag) {
            return &tlv;
        }
    }
    return nullptr;
}

} // namespace

TEST(ChfCdrAsn1, UnmappedNetworkFunctionalityEncodesEmpty) {
    chf::CdrRecord record{};
    record.nf_consumer_node_functionality = "NWDAF"; // real, confirmed: no TS 32.298 v18.8.0 value
    const auto bytes = chf::encode_chf_cdr(record, "some-uuid");
    EXPECT_TRUE(bytes.empty());
}

TEST(ChfCdrAsn1, TopLevelIsRealChargingFunctionRecordTag200) {
    chf::CdrRecord record{};
    record.nf_consumer_node_functionality = "SMF";
    record.charging_data_ref = "ref-1";
    record.invocation_sequence_number = 1;
    record.operation = "Release";
    record.invocation_time_stamp = 1755000000; // arbitrary fixed Unix time
    const auto bytes = chf::encode_chf_cdr(record, "chf-uuid");
    ASSERT_FALSE(bytes.empty());

    std::size_t offset = 0;
    const auto top = tcap_core::decode_tlv(bytes, offset);
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(top->tag_class, TagClass::kContext);
    EXPECT_TRUE(top->constructed);
    EXPECT_EQ(top->tag_number, 200u); // real chargingFunctionRecord tag, TS 32.298 §5.2.5.2
    EXPECT_EQ(offset, bytes.size());  // exactly one top-level TLV
}

TEST(ChfCdrAsn1, GenericReleaseRecordFieldsRoundTrip) {
    chf::CdrRecord record{};
    record.nf_consumer_node_functionality = "SMF";
    record.charging_data_ref = "charging-ref-42";
    record.invocation_sequence_number = 7;
    record.operation = "Release";
    record.subscriber_identifier = "imsi-999700000000001";
    record.invocation_time_stamp = 1755000000;

    const auto bytes = chf::encode_chf_cdr(record, "b91ffc16-5ec7-4d60-90eb-c00ecbee947d");
    ASSERT_FALSE(bytes.empty());

    std::size_t offset = 0;
    const auto top = tcap_core::decode_tlv(bytes, offset);
    ASSERT_TRUE(top.has_value());
    const auto fields = tcap_core::decode_tlvs(top->value);
    ASSERT_TRUE(fields.has_value());

    // [0] recordType -- real chargingFunctionRecord(200)
    const auto* record_type = find_context(*fields, 0);
    ASSERT_NE(record_type, nullptr);
    EXPECT_FALSE(record_type->constructed);
    EXPECT_EQ(tcap_core::decode_integer(record_type->value), 200);

    // [1] recordingNetworkFunctionID -- verbatim UUID string
    const auto* recording_nf = find_context(*fields, 1);
    ASSERT_NE(recording_nf, nullptr);
    EXPECT_EQ(std::string(recording_nf->value.begin(), recording_nf->value.end()),
              "b91ffc16-5ec7-4d60-90eb-c00ecbee947d");

    // [2] subscriberIdentifier -- SET { subscriptionIDType [0]=1 (eND-USER-IMSI), data [1] }
    const auto* subscriber = find_context(*fields, 2);
    ASSERT_NE(subscriber, nullptr);
    EXPECT_TRUE(subscriber->constructed);
    const auto subscriber_fields = tcap_core::decode_tlvs(subscriber->value);
    ASSERT_TRUE(subscriber_fields.has_value());
    const auto* id_type = find_context(*subscriber_fields, 0);
    ASSERT_NE(id_type, nullptr);
    EXPECT_EQ(tcap_core::decode_integer(id_type->value), 1); // eND-USER-IMSI
    const auto* id_data = find_context(*subscriber_fields, 1);
    ASSERT_NE(id_data, nullptr);
    // real, confirmed: the "imsi-" URI-style prefix is stripped, only the digit string remains
    EXPECT_EQ(std::string(id_data->value.begin(), id_data->value.end()), "999700000000001");

    // [3] nFunctionConsumerInformation -- SEQUENCE { networkFunctionality [0]=1 (sMF) }
    const auto* nf_info = find_context(*fields, 3);
    ASSERT_NE(nf_info, nullptr);
    EXPECT_TRUE(nf_info->constructed);
    const auto nf_info_fields = tcap_core::decode_tlvs(nf_info->value);
    ASSERT_TRUE(nf_info_fields.has_value());
    const auto* functionality = find_context(*nf_info_fields, 0);
    ASSERT_NE(functionality, nullptr);
    EXPECT_EQ(tcap_core::decode_integer(functionality->value), 1); // sMF

    // [9] causeForRecClosing -- real normalRelease(0), only present for a Release row
    const auto* cause = find_context(*fields, 9);
    ASSERT_NE(cause, nullptr);
    EXPECT_EQ(tcap_core::decode_integer(cause->value), 0);

    // [11] localRecordSequenceNumber
    const auto* local_seq = find_context(*fields, 11);
    ASSERT_NE(local_seq, nullptr);
    EXPECT_EQ(tcap_core::decode_integer(local_seq->value), 7);

    // [16] chargingSessionIdentifier -- verbatim charging_data_ref
    const auto* session_id = find_context(*fields, 16);
    ASSERT_NE(session_id, nullptr);
    EXPECT_EQ(std::string(session_id->value.begin(), session_id->value.end()), "charging-ref-42");

    // [40] invocationTimestamp -- real 9-byte BCD TimeStamp (TS 32.298 §5.2.1)
    const auto* invocation_ts = find_context(*fields, 40);
    ASSERT_NE(invocation_ts, nullptr);
    ASSERT_EQ(invocation_ts->value.size(), 9u);
    // 1755000000 unix = 2025-08-12 12:20:00 UTC -- year 25, month 08, day 12
    EXPECT_EQ(invocation_ts->value[0], 0x25);                           // year (BCD)
    EXPECT_EQ(invocation_ts->value[1], 0x08);                           // month (BCD)
    EXPECT_EQ(invocation_ts->value[2], 0x12);                           // day (BCD)
    EXPECT_EQ(invocation_ts->value[6], static_cast<std::uint8_t>('+')); // sign
    EXPECT_EQ(invocation_ts->value[7], 0x00); // UTC offset hours (this lab is always UTC)
    EXPECT_EQ(invocation_ts->value[8], 0x00); // UTC offset minutes

    // [6] recordOpeningTime is real, disclosed absent -- this test's own operation is "Release",
    // and recordOpeningTime is only populated for a "Create" row (see cdr_asn1.cpp's own comment).
    EXPECT_EQ(find_context(*fields, 6), nullptr);
    // [5] listOfMultipleUnitUsage is real, disclosed absent -- rating_group was never set.
    EXPECT_EQ(find_context(*fields, 5), nullptr);
}

TEST(ChfCdrAsn1, MultipleUnitUsageAndUsedUnitContainerRoundTrip) {
    chf::CdrRecord record{};
    record.nf_consumer_node_functionality = "SMF";
    record.charging_data_ref = "ref-2";
    record.invocation_sequence_number = 3;
    record.operation = "Update";
    record.rating_group = 5;
    record.used_total_volume = 123456;
    record.invocation_time_stamp = 1755000000;

    const auto bytes = chf::encode_chf_cdr(record, "chf-uuid");
    ASSERT_FALSE(bytes.empty());

    std::size_t offset = 0;
    const auto top = tcap_core::decode_tlv(bytes, offset);
    ASSERT_TRUE(top.has_value());
    const auto fields = tcap_core::decode_tlvs(top->value);
    ASSERT_TRUE(fields.has_value());

    // [5] listOfMultipleUnitUsage -- SEQUENCE OF MultipleUnitUsage, one real element (this
    // project's own rating engine only ever uses one rating group per session).
    const auto* list = find_context(*fields, 5);
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(list->constructed);
    const auto list_items = tcap_core::decode_tlvs(list->value);
    ASSERT_TRUE(list_items.has_value());
    ASSERT_EQ(list_items->size(), 1u);
    EXPECT_EQ((*list_items)[0].tag_class, TagClass::kUniversal);
    EXPECT_EQ((*list_items)[0].tag_number, tcap_core::UniversalTag::kSequence);

    const auto mu_fields = tcap_core::decode_tlvs((*list_items)[0].value);
    ASSERT_TRUE(mu_fields.has_value());
    const auto* rating_group = find_context(*mu_fields, 0);
    ASSERT_NE(rating_group, nullptr);
    EXPECT_EQ(tcap_core::decode_integer(rating_group->value), 5);

    const auto* used_containers = find_context(*mu_fields, 1);
    ASSERT_NE(used_containers, nullptr);
    EXPECT_TRUE(used_containers->constructed);
    const auto container_items = tcap_core::decode_tlvs(used_containers->value);
    ASSERT_TRUE(container_items.has_value());
    ASSERT_EQ(container_items->size(), 1u);

    const auto uc_fields = tcap_core::decode_tlvs((*container_items)[0].value);
    ASSERT_TRUE(uc_fields.has_value());
    const auto* total_volume = find_context(*uc_fields, 4);
    ASSERT_NE(total_volume, nullptr);
    EXPECT_EQ(tcap_core::decode_integer(total_volume->value), 123456);
    const auto* local_seq = find_context(*uc_fields, 9);
    ASSERT_NE(local_seq, nullptr);
    EXPECT_EQ(tcap_core::decode_integer(local_seq->value), 3);
    const auto* rating_indicator = find_context(*uc_fields, 10);
    ASSERT_NE(rating_indicator, nullptr);
    ASSERT_EQ(rating_indicator->value.size(), 1u);
    EXPECT_EQ(rating_indicator->value[0], 0xFF); // real, honest "always rated" -- see comment
}
