// Unit tests for libs/pfcp-core -- byte layouts confirmed against the real 3GPP TS 29.244 V14.3.0
// spec PDF (see pfcp_core/header.hpp's own comment for the version-gap disclosure and
// docs/DECISIONS.md ADR-0039).

#include "pfcp_core/common_ies.hpp"
#include "pfcp_core/header.hpp"
#include "pfcp_core/ie.hpp"
#include "pfcp_core/pfd_ies.hpp"
#include "pfcp_core/session_ies.hpp"

#include <gtest/gtest.h>

TEST(PfcpHeader, EncodesNodeRelatedHeaderWithCorrectByteLayout) {
    pfcp_core::Header h;
    h.has_seid = false;
    h.message_type = pfcp_core::MessageType::HeartbeatRequest;
    h.sequence_number = 0x010203;

    const auto bytes = pfcp_core::encode_header(h, /*ies_length=*/5);
    // version=1(001) in bits 8-6, spare=000, MP=0, S=0 -> octet1 = 0b00100000 = 0x20
    ASSERT_EQ(bytes.size(), 8u);
    EXPECT_EQ(bytes[0], 0x20);
    EXPECT_EQ(bytes[1], static_cast<std::uint8_t>(pfcp_core::MessageType::HeartbeatRequest));
    // message_length = overhead(4) + ies_length(5) = 9
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x09);
    EXPECT_EQ(bytes[4], 0x01);
    EXPECT_EQ(bytes[5], 0x02);
    EXPECT_EQ(bytes[6], 0x03);
    EXPECT_EQ(bytes[7], 0x00); // spare
}

TEST(PfcpHeader, NodeRelatedHeaderRoundTrips) {
    pfcp_core::Header h;
    h.has_seid = false;
    h.message_type = pfcp_core::MessageType::AssociationSetupResponse;
    h.sequence_number = 0xABCDEF;

    auto bytes = pfcp_core::encode_header(h, /*ies_length=*/20);
    bytes.resize(bytes.size() + 20); // simulate trailing IE bytes

    std::size_t offset = 0;
    std::uint16_t ies_length = 0;
    const auto decoded = pfcp_core::decode_header(bytes, offset, ies_length);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->has_seid);
    EXPECT_EQ(decoded->message_type, pfcp_core::MessageType::AssociationSetupResponse);
    EXPECT_EQ(decoded->sequence_number, 0xABCDEFu);
    EXPECT_EQ(offset, 8u);
    EXPECT_EQ(ies_length, 20u);
}

TEST(PfcpHeader, SessionRelatedHeaderRoundTrips) {
    pfcp_core::Header h;
    h.has_seid = true;
    h.seid = 0x0123456789ABCDEFULL;
    h.message_type = pfcp_core::MessageType::SessionEstablishmentRequest;
    h.sequence_number = 42;

    auto bytes = pfcp_core::encode_header(h, /*ies_length=*/7);
    ASSERT_EQ(bytes.size(), 16u);
    bytes.resize(bytes.size() + 7);

    std::size_t offset = 0;
    std::uint16_t ies_length = 0;
    const auto decoded = pfcp_core::decode_header(bytes, offset, ies_length);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->has_seid);
    EXPECT_EQ(decoded->seid, 0x0123456789ABCDEFULL);
    EXPECT_EQ(decoded->sequence_number, 42u);
    EXPECT_EQ(offset, 16u);
    EXPECT_EQ(ies_length, 7u);
}

TEST(PfcpHeader, RejectsTooShortBuffer) {
    std::vector<std::uint8_t> bytes = {0x20, 0x01, 0x00};
    std::size_t offset = 0;
    std::uint16_t ies_length = 0;
    EXPECT_FALSE(pfcp_core::decode_header(bytes, offset, ies_length).has_value());
}

TEST(PfcpHeader, RejectsUnrecognizedVersion) {
    std::vector<std::uint8_t> bytes = {0x40, 0x01, 0x00, 0x04, 0, 0, 0, 0}; // version=2
    std::size_t offset = 0;
    std::uint16_t ies_length = 0;
    EXPECT_FALSE(pfcp_core::decode_header(bytes, offset, ies_length).has_value());
}

TEST(PfcpIe, EncodeDecodeRoundTrips) {
    std::vector<std::uint8_t> out;
    pfcp_core::encode_ie(out, static_cast<std::uint16_t>(pfcp_core::IeType::Cause), {0x01});
    pfcp_core::encode_ie(
        out, static_cast<std::uint16_t>(pfcp_core::IeType::NodeId), {0x00, 10, 0, 0, 1});

    const auto ies = pfcp_core::decode_ies(out);
    ASSERT_TRUE(ies.has_value());
    ASSERT_EQ(ies->size(), 2u);
    EXPECT_EQ((*ies)[0].type, static_cast<std::uint16_t>(pfcp_core::IeType::Cause));
    EXPECT_EQ((*ies)[0].value, (std::vector<std::uint8_t>{0x01}));
    EXPECT_EQ((*ies)[1].type, static_cast<std::uint16_t>(pfcp_core::IeType::NodeId));

    const auto* cause_ie =
        pfcp_core::find_ie(*ies, static_cast<std::uint16_t>(pfcp_core::IeType::Cause));
    ASSERT_NE(cause_ie, nullptr);
    EXPECT_EQ(cause_ie->value, (std::vector<std::uint8_t>{0x01}));
    EXPECT_EQ(pfcp_core::find_ie(*ies, 9999), nullptr);
}

TEST(PfcpIe, DecodeRejectsTruncatedIe) {
    const std::vector<std::uint8_t> bytes = {
        0x00, 19, 0x00, 0x05, 0x01}; // declares length 5, has 1
    EXPECT_FALSE(pfcp_core::decode_ies(bytes).has_value());
}

TEST(PfcpCommonIes, CauseRoundTrips) {
    const auto bytes = pfcp_core::encode_cause(pfcp_core::Cause::RequestAccepted);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x01}));
    const auto decoded = pfcp_core::decode_cause(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, pfcp_core::Cause::RequestAccepted);
}

TEST(PfcpCommonIes, CauseSessionContextNotFoundRoundTrips) {
    // ADR-0071: real Table 8.2.1-1 value 65, added for Session Deletion's unknown-SEID case.
    const auto bytes = pfcp_core::encode_cause(pfcp_core::Cause::SessionContextNotFound);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{65}));
    const auto decoded = pfcp_core::decode_cause(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, pfcp_core::Cause::SessionContextNotFound);
}

TEST(PfcpCommonIes, RecoveryTimeStampRoundTrips) {
    const std::time_t now = 1754660000; // arbitrary fixed Unix time
    const auto bytes = pfcp_core::encode_recovery_time_stamp(now);
    ASSERT_EQ(bytes.size(), 4u);
    const auto decoded = pfcp_core::decode_recovery_time_stamp(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, now);
}

TEST(PfcpCommonIes, NodeIdIpv4RoundTrips) {
    const std::array<std::uint8_t, 4> ip{127, 0, 0, 5};
    const auto bytes = pfcp_core::encode_node_id_ipv4(ip);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x00, 127, 0, 0, 5}));
    const auto decoded = pfcp_core::decode_node_id_ipv4(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, ip);
}

TEST(PfcpCommonIes, NodeIdIpv4RejectsWrongType) {
    const std::vector<std::uint8_t> bytes = {0x02, 127, 0, 0, 5}; // type=2 (FQDN), not IPv4
    EXPECT_FALSE(pfcp_core::decode_node_id_ipv4(bytes).has_value());
}

TEST(PfcpCommonIes, FunctionFeaturesNoneAreCorrectLength) {
    EXPECT_EQ(pfcp_core::encode_up_function_features_none().size(), 2u);
    EXPECT_EQ(pfcp_core::encode_cp_function_features_none().size(), 1u);
}

TEST(PfcpSessionIes, SourceAndDestinationInterfaceRoundTrip) {
    const auto src = pfcp_core::encode_source_interface(pfcp_core::InterfaceValue::Access);
    EXPECT_EQ(src, (std::vector<std::uint8_t>{0x00}));
    const auto dst = pfcp_core::encode_destination_interface(pfcp_core::InterfaceValue::Core);
    EXPECT_EQ(dst, (std::vector<std::uint8_t>{0x01}));
    EXPECT_EQ(pfcp_core::decode_interface_value(src), pfcp_core::InterfaceValue::Access);
    EXPECT_EQ(pfcp_core::decode_interface_value(dst), pfcp_core::InterfaceValue::Core);
}

TEST(PfcpSessionIes, PdrIdRoundTrips) {
    const auto bytes = pfcp_core::encode_pdr_id(1);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x00, 0x01}));
    EXPECT_EQ(pfcp_core::decode_pdr_id(bytes), 1u);
}

TEST(PfcpSessionIes, PrecedenceRoundTrips) {
    const auto bytes = pfcp_core::encode_precedence(100);
    const auto decoded = pfcp_core::decode_precedence(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, 100u);
}

TEST(PfcpSessionIes, FarIdRoundTrips) {
    const auto bytes = pfcp_core::encode_far_id(1);
    const auto decoded = pfcp_core::decode_far_id(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, 1u);
}

TEST(PfcpSessionIes, ApplyActionForwardIsDetected) {
    const auto bytes = pfcp_core::encode_apply_action_forward();
    EXPECT_TRUE(pfcp_core::decode_apply_action_has_forward(bytes));
    EXPECT_FALSE(pfcp_core::decode_apply_action_has_forward({0x00})); // DROP only
}

TEST(PfcpSessionIes, FTeidChooseRequestRoundTrips) {
    const auto bytes = pfcp_core::encode_f_teid_choose_ipv4();
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x05})); // V4|CH
    EXPECT_TRUE(pfcp_core::decode_f_teid_is_choose_request(bytes));
}

TEST(PfcpSessionIes, FTeidAllocatedRoundTrips) {
    const std::array<std::uint8_t, 4> ip{10, 0, 0, 5};
    const auto bytes = pfcp_core::encode_f_teid_allocated_ipv4(0x12345678, ip);
    ASSERT_EQ(bytes.size(), 9u);
    EXPECT_FALSE(pfcp_core::decode_f_teid_is_choose_request(bytes));
    const auto decoded = pfcp_core::decode_f_teid_allocated_ipv4(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->teid, 0x12345678u);
    EXPECT_EQ(decoded->ipv4, ip);
}

TEST(PfcpSessionIes, FTeidAllocatedRejectsChooseFlagSet) {
    const auto choose_bytes = pfcp_core::encode_f_teid_choose_ipv4();
    EXPECT_FALSE(pfcp_core::decode_f_teid_allocated_ipv4(choose_bytes).has_value());
}

TEST(PfcpSessionIes, FSeidRoundTrips) {
    pfcp_core::FSeid seid;
    seid.seid = 0x0123456789ABCDEFULL;
    seid.ipv4 = {127, 0, 0, 1};
    const auto bytes = pfcp_core::encode_f_seid_ipv4(seid);
    ASSERT_EQ(bytes.size(), 13u);
    const auto decoded = pfcp_core::decode_f_seid_ipv4(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->seid, seid.seid);
    EXPECT_EQ(decoded->ipv4, seid.ipv4);
}

TEST(PfcpSessionIes, GroupedIeRoundTripsViaExistingIeCodec) {
    // Create PDR (grouped): PDR ID + Precedence + PDI(grouped: Source Interface) + FAR ID --
    // confirms encode_ie/decode_ies (built for flat IEs) work unmodified for grouped IEs too, per
    // TS 29.244 §7.2.3.3's own statement that a grouped IE's value is just concatenated child IEs.
    std::vector<std::uint8_t> pdi;
    pfcp_core::encode_ie(pdi,
                         static_cast<std::uint16_t>(pfcp_core::IeType::SourceInterface),
                         pfcp_core::encode_source_interface(pfcp_core::InterfaceValue::Access));

    std::vector<std::uint8_t> pdr;
    pfcp_core::encode_ie(
        pdr, static_cast<std::uint16_t>(pfcp_core::IeType::PdrId), pfcp_core::encode_pdr_id(1));
    pfcp_core::encode_ie(pdr,
                         static_cast<std::uint16_t>(pfcp_core::IeType::Precedence),
                         pfcp_core::encode_precedence(100));
    pfcp_core::encode_ie(pdr, static_cast<std::uint16_t>(pfcp_core::IeType::Pdi), pdi);
    pfcp_core::encode_ie(
        pdr, static_cast<std::uint16_t>(pfcp_core::IeType::FarId), pfcp_core::encode_far_id(1));

    const auto pdr_ies = pfcp_core::decode_ies(pdr);
    ASSERT_TRUE(pdr_ies.has_value());
    ASSERT_EQ(pdr_ies->size(), 4u);

    const auto* pdi_ie =
        pfcp_core::find_ie(*pdr_ies, static_cast<std::uint16_t>(pfcp_core::IeType::Pdi));
    ASSERT_NE(pdi_ie, nullptr);
    const auto pdi_ies = pfcp_core::decode_ies(pdi_ie->value);
    ASSERT_TRUE(pdi_ies.has_value());
    ASSERT_EQ(pdi_ies->size(), 1u);
    const auto* src_if_ie = pfcp_core::find_ie(
        *pdi_ies, static_cast<std::uint16_t>(pfcp_core::IeType::SourceInterface));
    ASSERT_NE(src_if_ie, nullptr);
    EXPECT_EQ(pfcp_core::decode_interface_value(src_if_ie->value),
              pfcp_core::InterfaceValue::Access);
}

TEST(PfcpSessionIes, UrrIdRoundTrips) {
    const auto bytes = pfcp_core::encode_urr_id(1);
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x00, 0x00, 0x00, 0x01}));
    EXPECT_EQ(pfcp_core::decode_urr_id(bytes), 1u);
}

TEST(PfcpSessionIes, UrSeqnRoundTrips) {
    const auto bytes = pfcp_core::encode_ur_seqn(42);
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(pfcp_core::decode_ur_seqn(bytes), 42u);
}

TEST(PfcpSessionIes, MeasurementMethodVolumeSetsVolumBit) {
    const auto bytes = pfcp_core::encode_measurement_method_volume();
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0] & 0x02, 0x02); // bit 2 - VOLUM, TS 29.244 Figure 8.2.40-1
}

TEST(PfcpSessionIes, ReportingTriggersVolumeSetsVolthAndVolqu) {
    const auto bytes = pfcp_core::encode_reporting_triggers_volume();
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0] & 0x02, 0x02); // octet 5 bit 2 - VOLTH
    EXPECT_EQ(bytes[1] & 0x01, 0x01); // octet 6 bit 1 - VOLQU
}

TEST(PfcpSessionIes, VolumeTotalRoundTrips) {
    // TS 29.244 Figure 8.2.13-1 (Volume Threshold) / 8.2.50-1 (Volume Quota) / 8.2.44-1 (Volume
    // Measurement): all three share this exact TOVOL-flag + Unsigned64 layout.
    const auto bytes = pfcp_core::encode_volume_total(10'000'000'000ULL); // 10 GB, decimal
    ASSERT_EQ(bytes.size(), 9u);
    EXPECT_EQ(bytes[0] & 0x01, 0x01); // TOVOL bit
    EXPECT_EQ(pfcp_core::decode_volume_total(bytes), 10'000'000'000ULL);
}

TEST(PfcpSessionIes, VolumeTotalDecodeRejectsMissingTovolBit) {
    std::vector<std::uint8_t> bytes(9, 0); // flag octet 0 -- no TOVOL bit set
    EXPECT_FALSE(pfcp_core::decode_volume_total(bytes).has_value());
}

TEST(PfcpSessionIes, ReportTypeDetectsUsageReportBit) {
    EXPECT_TRUE(pfcp_core::decode_report_type_has_usage_report({0x02}));  // bit 2 - USAR
    EXPECT_FALSE(pfcp_core::decode_report_type_has_usage_report({0x01})); // bit 1 - DLDR only
}

TEST(PfcpSessionIes, EncodeReportTypeUsageReportRoundTrips) {
    EXPECT_TRUE(pfcp_core::decode_report_type_has_usage_report(
        pfcp_core::encode_report_type_usage_report()));
}

TEST(PfcpSessionIes, UsageReportTriggerDecodesVolumeThreshold) {
    EXPECT_EQ(pfcp_core::decode_usage_report_trigger({0x02, 0x00}),
              pfcp_core::UsageReportTriggerValue::VolumeThreshold);
}

TEST(PfcpSessionIes, UsageReportTriggerDecodesVolumeQuotaExhausted) {
    EXPECT_EQ(pfcp_core::decode_usage_report_trigger({0x00, 0x01}),
              pfcp_core::UsageReportTriggerValue::VolumeQuotaExhausted);
}

TEST(PfcpSessionIes, UsageReportTriggerDecodesOtherForUnrecognizedBits) {
    EXPECT_EQ(pfcp_core::decode_usage_report_trigger({0x01, 0x00}), // PERIO only
              pfcp_core::UsageReportTriggerValue::Other);
}

TEST(PfcpSessionIes, EncodeUsageReportTriggerVolthRoundTrips) {
    EXPECT_EQ(
        pfcp_core::decode_usage_report_trigger(pfcp_core::encode_usage_report_trigger_volth()),
        pfcp_core::UsageReportTriggerValue::VolumeThreshold);
}

TEST(PfcpSessionIes, EncodeUsageReportTriggerVolquRoundTrips) {
    EXPECT_EQ(
        pfcp_core::decode_usage_report_trigger(pfcp_core::encode_usage_report_trigger_volqu()),
        pfcp_core::UsageReportTriggerValue::VolumeQuotaExhausted);
}

TEST(PfcpSessionIes, EncodeUsageReportTriggerTermrSetsOctet6Bit4) {
    // TS 29.244 §8.2.41 octet 6 bit 4 = TERMR = 0x08 -- decode_usage_report_trigger has no
    // TERMR-specific enum value (this project's disclosed narrow scope, see its own header
    // comment), so this checks the real wire bytes directly rather than round-tripping through it.
    EXPECT_EQ(pfcp_core::encode_usage_report_trigger_termr(),
              (std::vector<std::uint8_t>{0x00, 0x08}));
}

// --- ADR-0071 (gap-closure Tier 1d): QER ID, BAR ID, Gate Status, MBR ---

TEST(PfcpSessionIes, QerIdRoundTrips) {
    const auto encoded = pfcp_core::encode_qer_id(42);
    EXPECT_EQ(encoded.size(), 4u);
    const auto decoded = pfcp_core::decode_qer_id(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, 42u);
}

TEST(PfcpSessionIes, BarIdRoundTripsAsSingleOctet) {
    const auto encoded = pfcp_core::encode_bar_id(7);
    EXPECT_EQ(encoded.size(), 1u); // real, confirmed: 1 octet, not 4 like QER/FAR/URR ID
    const auto decoded = pfcp_core::decode_bar_id(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, 7u);
}

TEST(PfcpSessionIes, GateStatusBothOpenRoundTrips) {
    pfcp_core::GateStatus gs;
    gs.ul_closed = false;
    gs.dl_closed = false;
    const auto encoded = pfcp_core::encode_gate_status(gs);
    EXPECT_EQ(encoded, (std::vector<std::uint8_t>{0x00}));
    const auto decoded = pfcp_core::decode_gate_status(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->ul_closed);
    EXPECT_FALSE(decoded->dl_closed);
}

TEST(PfcpSessionIes, GateStatusUlClosedDlOpenRoundTrips) {
    pfcp_core::GateStatus gs;
    gs.ul_closed = true;
    gs.dl_closed = false;
    const auto encoded = pfcp_core::encode_gate_status(gs);
    // Real bit positions confirmed from the spec figure: UL Gate in bits 4-3 -> 0x01 << 2 = 0x04.
    EXPECT_EQ(encoded, (std::vector<std::uint8_t>{0x04}));
    const auto decoded = pfcp_core::decode_gate_status(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->ul_closed);
    EXPECT_FALSE(decoded->dl_closed);
}

TEST(PfcpSessionIes, GateStatusBothClosedRoundTrips) {
    pfcp_core::GateStatus gs;
    gs.ul_closed = true;
    gs.dl_closed = true;
    const auto decoded = pfcp_core::decode_gate_status(pfcp_core::encode_gate_status(gs));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->ul_closed);
    EXPECT_TRUE(decoded->dl_closed);
}

TEST(PfcpSessionIes, MbrRoundTrips) {
    pfcp_core::Mbr mbr;
    mbr.ul_kbps = 10000;  // 10 Mbps
    mbr.dl_kbps = 100000; // 100 Mbps
    const auto encoded = pfcp_core::encode_mbr(mbr);
    EXPECT_EQ(encoded.size(), 10u); // real, confirmed fixed 10-octet IE
    const auto decoded = pfcp_core::decode_mbr(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->ul_kbps, 10000u);
    EXPECT_EQ(decoded->dl_kbps, 100000u);
}

TEST(PfcpSessionIes, MbrRoundTripsLargeValue) {
    pfcp_core::Mbr mbr;
    mbr.ul_kbps = 0xFFFFFFFFFFULL; // max real 5-octet value (2^40 - 1)
    mbr.dl_kbps = 1;
    const auto decoded = pfcp_core::decode_mbr(pfcp_core::encode_mbr(mbr));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->ul_kbps, 0xFFFFFFFFFFULL);
    EXPECT_EQ(decoded->dl_kbps, 1u);
}

// Gap-closure task #107 part 2 (ADR-0086): PFD Management IEs.

TEST(PfcpPfdIes, ApplicationIdRoundTrips) {
    const auto encoded = pfcp_core::encode_application_id("app-exampleapp");
    EXPECT_EQ(pfcp_core::decode_application_id(encoded), "app-exampleapp");
}

TEST(PfcpPfdIes, PfdContentsRoundTripsAllFourFields) {
    pfcp_core::PfdContents contents;
    contents.flow_description =
        std::vector<std::uint8_t>{'p', 'e', 'r', 'm', 'i', 't', ' ', 'o', 'u', 't'};
    contents.url = std::vector<std::uint8_t>{'h', 't', 't', 'p', ':', '/', '/', 'x'};
    contents.domain_name = std::vector<std::uint8_t>{'e', 'x', '.', 'c', 'o', 'm'};
    contents.custom_content = std::vector<std::uint8_t>{0x01, 0x02, 0x03};

    const auto decoded = pfcp_core::decode_pfd_contents(pfcp_core::encode_pfd_contents(contents));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->flow_description.has_value());
    EXPECT_EQ(*decoded->flow_description, *contents.flow_description);
    ASSERT_TRUE(decoded->url.has_value());
    EXPECT_EQ(*decoded->url, *contents.url);
    ASSERT_TRUE(decoded->domain_name.has_value());
    EXPECT_EQ(*decoded->domain_name, *contents.domain_name);
    ASSERT_TRUE(decoded->custom_content.has_value());
    EXPECT_EQ(*decoded->custom_content, *contents.custom_content);
}

TEST(PfcpPfdIes, PfdContentsRoundTripsOnlyFlowDescription) {
    pfcp_core::PfdContents contents;
    contents.flow_description = std::vector<std::uint8_t>{'x'};

    const auto decoded = pfcp_core::decode_pfd_contents(pfcp_core::encode_pfd_contents(contents));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->flow_description.has_value());
    EXPECT_EQ(*decoded->flow_description, *contents.flow_description);
    EXPECT_FALSE(decoded->url.has_value());
    EXPECT_FALSE(decoded->domain_name.has_value());
    EXPECT_FALSE(decoded->custom_content.has_value());
}

TEST(PfcpPfdIes, PfdContentsEmptyHasNoFieldsSet) {
    const pfcp_core::PfdContents contents;
    const auto encoded = pfcp_core::encode_pfd_contents(contents);
    EXPECT_EQ(encoded.size(), 2u); // just the flags + spare octets, no fields
    const auto decoded = pfcp_core::decode_pfd_contents(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->flow_description.has_value());
    EXPECT_FALSE(decoded->url.has_value());
    EXPECT_FALSE(decoded->domain_name.has_value());
    EXPECT_FALSE(decoded->custom_content.has_value());
}

TEST(PfcpPfdIes, PfdContentsRejectsTooShortBuffer) {
    EXPECT_FALSE(pfcp_core::decode_pfd_contents(std::vector<std::uint8_t>{0x01}).has_value());
}

TEST(PfcpPfdIes, PfdContentsRejectsFlagSetWithTruncatedField) {
    // FD flag set, but no length-prefixed field bytes follow.
    const std::vector<std::uint8_t> bytes{0x01, 0x00};
    EXPECT_FALSE(pfcp_core::decode_pfd_contents(bytes).has_value());
}

TEST(PfcpPfdIes, ApplicationIdsPfdsGroupRoundTripsViaExistingIeCodec) {
    // "Application ID's PFDs" (58) and "PFD context" (59) are grouped IEs with no dedicated codec
    // (same choice CreatePdr/CreateFar already made) -- this confirms the generic encode_ie/
    // decode_ies pair round-trips a real, nested Application ID + PFD context + PFD Contents tree
    // correctly, matching PfcpSessionIes.GroupedIeRoundTripsViaExistingIeCodec's own precedent.
    pfcp_core::PfdContents contents;
    contents.flow_description = std::vector<std::uint8_t>{'x'};

    std::vector<std::uint8_t> pfd_context_ies;
    pfcp_core::encode_ie(pfd_context_ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::PfdContents),
                         pfcp_core::encode_pfd_contents(contents));

    std::vector<std::uint8_t> app_pfds_ies;
    pfcp_core::encode_ie(app_pfds_ies,
                         static_cast<std::uint16_t>(pfcp_core::IeType::ApplicationId),
                         pfcp_core::encode_application_id("app-x"));
    pfcp_core::encode_ie(
        app_pfds_ies, static_cast<std::uint16_t>(pfcp_core::IeType::PfdContext), pfd_context_ies);

    const auto decoded = pfcp_core::decode_ies(app_pfds_ies);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2u);
    EXPECT_EQ((*decoded)[0].type, static_cast<std::uint16_t>(pfcp_core::IeType::ApplicationId));
    EXPECT_EQ(pfcp_core::decode_application_id((*decoded)[0].value), "app-x");
    EXPECT_EQ((*decoded)[1].type, static_cast<std::uint16_t>(pfcp_core::IeType::PfdContext));

    const auto inner = pfcp_core::decode_ies((*decoded)[1].value);
    ASSERT_TRUE(inner.has_value());
    ASSERT_EQ(inner->size(), 1u);
    EXPECT_EQ((*inner)[0].type, static_cast<std::uint16_t>(pfcp_core::IeType::PfdContents));
    const auto inner_contents = pfcp_core::decode_pfd_contents((*inner)[0].value);
    ASSERT_TRUE(inner_contents.has_value());
    ASSERT_TRUE(inner_contents->flow_description.has_value());
    EXPECT_EQ(*inner_contents->flow_description, *contents.flow_description);
}
