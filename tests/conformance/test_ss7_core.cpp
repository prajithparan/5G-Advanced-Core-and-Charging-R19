// Unit tests for libs/ss7-core -- byte layouts cross-checked against RFC 4666's own real primary
// text (M3UA) and Osmocom's real vendored struct definitions (SCCP, arms-length reference only --
// see ss7_core/sccp_dictionary.hpp's own comment and docs/DECISIONS.md's Stage 5 ADR update for
// the full disclosure of what is/isn't primary-spec-text-cited).

#include "ss7_core/m3ua_asp.hpp"
#include "ss7_core/m3ua_dictionary.hpp"
#include "ss7_core/m3ua_header.hpp"
#include "ss7_core/m3ua_protocol_data.hpp"
#include "ss7_core/m3ua_tlv.hpp"
#include "ss7_core/sccp_address.hpp"
#include "ss7_core/sccp_dictionary.hpp"
#include "ss7_core/sccp_udt.hpp"

#include <gtest/gtest.h>

TEST(M3uaHeader, EncodesDataHeaderWithCorrectByteLayout) {
    ss7_core::M3uaHeader h;
    h.message_class = ss7_core::dictionary::MessageClass::kTransfer;
    h.message_type = ss7_core::dictionary::TransferMessageType::kData;

    const auto bytes = ss7_core::encode_m3ua_header(h, /*payload_length=*/16);
    ASSERT_EQ(bytes.size(), 8u);
    EXPECT_EQ(bytes[0], ss7_core::kM3uaVersion);
    EXPECT_EQ(bytes[1], 0x00); // Reserved
    EXPECT_EQ(bytes[2], 1u);   // Message Class = Transfer
    EXPECT_EQ(bytes[3], 1u);   // Message Type = DATA
    // Message Length = 8 (header) + 16 (payload) = 24 = 0x00000018
    EXPECT_EQ(bytes[4], 0x00);
    EXPECT_EQ(bytes[5], 0x00);
    EXPECT_EQ(bytes[6], 0x00);
    EXPECT_EQ(bytes[7], 0x18);
}

TEST(M3uaHeader, RoundTrips) {
    ss7_core::M3uaHeader h;
    h.message_class = ss7_core::dictionary::MessageClass::kTransfer;
    h.message_type = ss7_core::dictionary::TransferMessageType::kData;

    auto bytes = ss7_core::encode_m3ua_header(h, /*payload_length=*/4);
    bytes.resize(bytes.size() + 4);

    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    const auto decoded = ss7_core::decode_m3ua_header(bytes, offset, payload_length);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->message_class, ss7_core::dictionary::MessageClass::kTransfer);
    EXPECT_EQ(decoded->message_type, ss7_core::dictionary::TransferMessageType::kData);
    EXPECT_EQ(payload_length, 4u);
    EXPECT_EQ(offset, 8u);
}

TEST(M3uaHeader, RejectsWrongVersion) {
    std::vector<std::uint8_t> bytes(8, 0);
    bytes[0] = 2; // not kM3uaVersion
    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    EXPECT_FALSE(ss7_core::decode_m3ua_header(bytes, offset, payload_length).has_value());
}

TEST(M3uaHeader, RejectsTooShortBuffer) {
    std::vector<std::uint8_t> bytes(4, 0);
    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    EXPECT_FALSE(ss7_core::decode_m3ua_header(bytes, offset, payload_length).has_value());
}

TEST(M3uaTlv, RoundTripsWithPadding) {
    std::vector<std::uint8_t> out;
    ss7_core::M3uaTlv tlv;
    tlv.tag = ss7_core::dictionary::ParamTag::kRoutingContext;
    tlv.value = {0x00, 0x00, 0x00, 0x01, 0x02}; // 5 bytes -> needs 3 padding bytes
    ss7_core::encode_m3ua_tlv(out, tlv);

    // Tag(2) + Length(2) + Value(5) + Padding(3) = 12
    ASSERT_EQ(out.size(), 12u);
    EXPECT_EQ(out[0], 0x00);
    EXPECT_EQ(out[1], 0x06); // kRoutingContext = 0x0006
    EXPECT_EQ(out[2], 0x00);
    EXPECT_EQ(out[3], 9u); // Length = 4 (tag+length) + 5 (value), padding NOT counted
    EXPECT_EQ(out[9], 0x00);
    EXPECT_EQ(out[10], 0x00);
    EXPECT_EQ(out[11], 0x00);

    const auto decoded = ss7_core::decode_m3ua_tlvs(out);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].tag, ss7_core::dictionary::ParamTag::kRoutingContext);
    EXPECT_EQ((*decoded)[0].value, tlv.value);
}

TEST(M3uaTlv, DecodesMultipleParameters) {
    std::vector<std::uint8_t> out;
    ss7_core::M3uaTlv na;
    na.tag = ss7_core::dictionary::ParamTag::kNetworkAppearance;
    na.value = ss7_core::encode_m3ua_uint32(42);
    ss7_core::encode_m3ua_tlv(out, na);

    ss7_core::M3uaTlv pd;
    pd.tag = ss7_core::dictionary::ParamTag::kProtocolData;
    pd.value = {0x01, 0x02, 0x03};
    ss7_core::encode_m3ua_tlv(out, pd);

    const auto decoded = ss7_core::decode_m3ua_tlvs(out);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2u);

    const auto* found_na =
        ss7_core::find_m3ua_tlv(*decoded, ss7_core::dictionary::ParamTag::kNetworkAppearance);
    ASSERT_NE(found_na, nullptr);
    EXPECT_EQ(ss7_core::decode_m3ua_uint32(found_na->value).value_or(0), 42u);

    const auto* found_pd =
        ss7_core::find_m3ua_tlv(*decoded, ss7_core::dictionary::ParamTag::kProtocolData);
    ASSERT_NE(found_pd, nullptr);
    EXPECT_EQ(found_pd->value, pd.value);
}

TEST(M3uaTlv, RejectsAvpLengthPastEndOfBuffer) {
    std::vector<std::uint8_t> bytes = {0x00, 0x06, 0x00, 0xFF}; // Length=255, way past buffer
    EXPECT_FALSE(ss7_core::decode_m3ua_tlvs(bytes).has_value());
}

TEST(M3uaTlv, EmptyBufferDecodesToZeroParameters) {
    const auto decoded = ss7_core::decode_m3ua_tlvs({});
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->empty());
}

TEST(M3uaProtocolData, RoundTrips) {
    ss7_core::M3uaProtocolData data;
    data.opc = 0x00010203;
    data.dpc = 0x04050607;
    data.si = ss7_core::dictionary::ServiceIndicator::kSccp;
    data.ni = 2;
    data.mp = 0;
    data.sls = 5;
    data.user_protocol_data = {0xDE, 0xAD, 0xBE, 0xEF};

    const auto bytes = ss7_core::encode_m3ua_protocol_data(data);
    ASSERT_EQ(bytes.size(), 12u + 4u);
    EXPECT_EQ(bytes[8], ss7_core::dictionary::ServiceIndicator::kSccp);

    const auto decoded = ss7_core::decode_m3ua_protocol_data(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->opc, 0x00010203u);
    EXPECT_EQ(decoded->dpc, 0x04050607u);
    EXPECT_EQ(decoded->si, ss7_core::dictionary::ServiceIndicator::kSccp);
    EXPECT_EQ(decoded->ni, 2u);
    EXPECT_EQ(decoded->sls, 5u);
    EXPECT_EQ(decoded->user_protocol_data, data.user_protocol_data);
}

TEST(SccpAddress, RoundTripsPointCodeAndSsn) {
    ss7_core::SccpAddress addr;
    addr.point_code_present = true;
    addr.ssn_present = true;
    addr.routing_indicator = ss7_core::dictionary::RoutingIndicator::kRouteOnSsn;
    addr.point_code = 0x1A2B & 0x3FFF; // 14-bit
    addr.ssn = ss7_core::dictionary::SubsystemNumber::kHlr;

    const auto bytes = ss7_core::encode_sccp_address(addr);
    ASSERT_EQ(bytes.size(), 4u); // indicator(1) + point code(2) + ssn(1)

    const auto decoded = ss7_core::decode_sccp_address(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->point_code_present);
    EXPECT_TRUE(decoded->ssn_present);
    EXPECT_EQ(decoded->routing_indicator, ss7_core::dictionary::RoutingIndicator::kRouteOnSsn);
    EXPECT_EQ(decoded->point_code, addr.point_code);
    EXPECT_EQ(decoded->ssn, ss7_core::dictionary::SubsystemNumber::kHlr);
}

TEST(SccpAddress, SsnOnlyNoPointCode) {
    ss7_core::SccpAddress addr;
    addr.point_code_present = false;
    addr.ssn_present = true;
    addr.routing_indicator = ss7_core::dictionary::RoutingIndicator::kRouteOnSsn;
    addr.ssn = ss7_core::dictionary::SubsystemNumber::kVlr;

    const auto bytes = ss7_core::encode_sccp_address(addr);
    ASSERT_EQ(bytes.size(), 2u); // indicator(1) + ssn(1)

    const auto decoded = ss7_core::decode_sccp_address(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->point_code_present);
    EXPECT_TRUE(decoded->ssn_present);
    EXPECT_EQ(decoded->ssn, ss7_core::dictionary::SubsystemNumber::kVlr);
}

TEST(SccpAddress, RejectsGlobalTitleAddress) {
    // indicator octet with GTI (bits 2-5) = 4 -- real, disclosed "not implemented" rejection.
    std::vector<std::uint8_t> bytes = {static_cast<std::uint8_t>(4 << 2)};
    EXPECT_FALSE(ss7_core::decode_sccp_address(bytes).has_value());
}

TEST(SccpUdt, RoundTrips) {
    ss7_core::SccpUdt udt;
    udt.protocol_class = ss7_core::dictionary::ProtocolClass::kClass0;
    udt.called_party.point_code_present = false;
    udt.called_party.ssn_present = true;
    udt.called_party.ssn = ss7_core::dictionary::SubsystemNumber::kHlr;
    udt.calling_party.point_code_present = false;
    udt.calling_party.ssn_present = true;
    udt.calling_party.ssn = ss7_core::dictionary::SubsystemNumber::kMsc;
    udt.data = {0x01, 0x02, 0x03, 0x04, 0x05};

    const auto bytes = ss7_core::encode_sccp_udt(udt);
    EXPECT_EQ(bytes[0], ss7_core::dictionary::MessageType::kUdt);

    const auto decoded = ss7_core::decode_sccp_udt(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->protocol_class, ss7_core::dictionary::ProtocolClass::kClass0);
    EXPECT_TRUE(decoded->called_party.ssn_present);
    EXPECT_EQ(decoded->called_party.ssn, ss7_core::dictionary::SubsystemNumber::kHlr);
    EXPECT_TRUE(decoded->calling_party.ssn_present);
    EXPECT_EQ(decoded->calling_party.ssn, ss7_core::dictionary::SubsystemNumber::kMsc);
    EXPECT_EQ(decoded->data, udt.data);
}

TEST(SccpUdt, RoundTripsWithPointCodeAddressing) {
    ss7_core::SccpUdt udt;
    udt.protocol_class = ss7_core::dictionary::ProtocolClass::kClass0;
    udt.called_party.point_code_present = true;
    udt.called_party.point_code = 1234 & 0x3FFF;
    udt.called_party.ssn_present = true;
    udt.called_party.ssn = ss7_core::dictionary::SubsystemNumber::kVlr;
    udt.calling_party.point_code_present = true;
    udt.calling_party.point_code = 5678 & 0x3FFF;
    udt.calling_party.ssn_present = true;
    udt.calling_party.ssn = ss7_core::dictionary::SubsystemNumber::kHlr;
    udt.data.assign(20, 0xAB); // exercise pointer arithmetic with a larger data field

    const auto bytes = ss7_core::encode_sccp_udt(udt);
    const auto decoded = ss7_core::decode_sccp_udt(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->called_party.point_code, udt.called_party.point_code);
    EXPECT_EQ(decoded->calling_party.point_code, udt.calling_party.point_code);
    EXPECT_EQ(decoded->data, udt.data);
}

TEST(SccpUdt, RejectsWrongMessageType) {
    std::vector<std::uint8_t> bytes = {ss7_core::dictionary::MessageType::kCr, 0, 0, 0, 0};
    EXPECT_FALSE(ss7_core::decode_sccp_udt(bytes).has_value());
}

TEST(SccpUdt, RejectsTooShortBuffer) {
    std::vector<std::uint8_t> bytes = {ss7_core::dictionary::MessageType::kUdt, 0};
    EXPECT_FALSE(ss7_core::decode_sccp_udt(bytes).has_value());
}

TEST(M3uaAsp, AspUpRoundTripsWithIdentifierAndInfoString) {
    ss7_core::AspStateMessage msg;
    msg.asp_identifier = 42;
    msg.info_string = "5gc-r19-chf";

    const auto bytes =
        ss7_core::encode_asp_state_message(ss7_core::dictionary::AspsmMessageType::kAspUp, msg);

    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    const auto header = ss7_core::decode_m3ua_header(bytes, offset, payload_length);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->message_class, ss7_core::dictionary::MessageClass::kAspsm);
    EXPECT_EQ(header->message_type, ss7_core::dictionary::AspsmMessageType::kAspUp);

    const auto decoded =
        ss7_core::decode_asp_state_message(ss7_core::dictionary::AspsmMessageType::kAspUp, bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->asp_identifier.has_value());
    EXPECT_EQ(*decoded->asp_identifier, 42u);
    ASSERT_TRUE(decoded->info_string.has_value());
    EXPECT_EQ(*decoded->info_string, "5gc-r19-chf");
}

TEST(M3uaAsp, AspUpAckRoundTripsWithNoOptionalParams) {
    ss7_core::AspStateMessage msg;
    const auto bytes =
        ss7_core::encode_asp_state_message(ss7_core::dictionary::AspsmMessageType::kAspUpAck, msg);

    const auto decoded = ss7_core::decode_asp_state_message(
        ss7_core::dictionary::AspsmMessageType::kAspUpAck, bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->asp_identifier.has_value());
    EXPECT_FALSE(decoded->info_string.has_value());
}

TEST(M3uaAsp, RejectsMismatchedAspStateMessageType) {
    ss7_core::AspStateMessage msg;
    const auto bytes =
        ss7_core::encode_asp_state_message(ss7_core::dictionary::AspsmMessageType::kAspUp, msg);
    EXPECT_FALSE(
        ss7_core::decode_asp_state_message(ss7_core::dictionary::AspsmMessageType::kAspDown, bytes)
            .has_value());
}

TEST(M3uaAsp, AspActiveRoundTripsWithTrafficModeAndRoutingContext) {
    ss7_core::AspTrafficMessage msg;
    msg.traffic_mode_type = ss7_core::dictionary::TrafficModeType::kLoadshare;
    msg.routing_context = std::vector<std::uint32_t>{100, 200};

    const auto bytes = ss7_core::encode_asp_traffic_message(
        ss7_core::dictionary::AsptmMessageType::kAspActive, msg);

    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    const auto header = ss7_core::decode_m3ua_header(bytes, offset, payload_length);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->message_class, ss7_core::dictionary::MessageClass::kAsptm);

    const auto decoded = ss7_core::decode_asp_traffic_message(
        ss7_core::dictionary::AsptmMessageType::kAspActive, bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->traffic_mode_type.has_value());
    EXPECT_EQ(*decoded->traffic_mode_type, ss7_core::dictionary::TrafficModeType::kLoadshare);
    ASSERT_TRUE(decoded->routing_context.has_value());
    EXPECT_EQ(*decoded->routing_context, (std::vector<std::uint32_t>{100, 200}));
}

TEST(M3uaAsp, AspInactiveAckRoundTripsWithNoTrafficModeType) {
    ss7_core::AspTrafficMessage msg;
    const auto bytes = ss7_core::encode_asp_traffic_message(
        ss7_core::dictionary::AsptmMessageType::kAspInactiveAck, msg);

    const auto decoded = ss7_core::decode_asp_traffic_message(
        ss7_core::dictionary::AsptmMessageType::kAspInactiveAck, bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->traffic_mode_type.has_value());
}
