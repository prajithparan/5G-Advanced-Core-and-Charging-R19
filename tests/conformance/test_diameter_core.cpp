// Unit tests for libs/diameter-core -- byte layouts cross-checked against freeDiameter's real
// vendored struct definitions (see diameter_core/header.hpp's and avp.hpp's own comments and
// docs/DECISIONS.md ADR-0059 for the disclosure of what is/isn't spec-text-cited).

#include "diameter_core/avp.hpp"
#include "diameter_core/dictionary.hpp"
#include "diameter_core/header.hpp"

#include <gtest/gtest.h>

TEST(DiameterHeader, EncodesCerWithCorrectByteLayout) {
    diameter_core::Header h;
    h.flags = diameter_core::CommandFlag::kRequest | diameter_core::CommandFlag::kProxiable;
    h.command_code = diameter_core::dictionary::Command::kCapabilitiesExchange;
    h.application_id = 0;
    h.hop_by_hop_id = 0x11223344;
    h.end_to_end_id = 0x55667788;

    const auto bytes = diameter_core::encode_header(h, /*avps_length=*/16);
    ASSERT_EQ(bytes.size(), 20u);
    EXPECT_EQ(bytes[0], diameter_core::kDiameterVersion);
    // message_length = 20 (header) + 16 (avps) = 36 = 0x000024
    EXPECT_EQ(bytes[1], 0x00);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x24);
    EXPECT_EQ(bytes[4], 0xC0u); // R|P
    // command code 257 = 0x000101
    EXPECT_EQ(bytes[5], 0x00);
    EXPECT_EQ(bytes[6], 0x01);
    EXPECT_EQ(bytes[7], 0x01);
}

TEST(DiameterHeader, RoundTrips) {
    diameter_core::Header h;
    h.flags = diameter_core::CommandFlag::kRequest;
    h.command_code = diameter_core::dictionary::Command::kCreditControl;
    h.application_id = 4; // Diameter Credit-Control Application
    h.hop_by_hop_id = 42;
    h.end_to_end_id = 99;

    auto bytes = diameter_core::encode_header(h, /*avps_length=*/12);
    bytes.resize(bytes.size() + 12);

    std::size_t offset = 0;
    std::uint32_t avps_length = 0;
    const auto decoded = diameter_core::decode_header(bytes, offset, avps_length);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->flags, diameter_core::CommandFlag::kRequest);
    EXPECT_EQ(decoded->command_code, diameter_core::dictionary::Command::kCreditControl);
    EXPECT_EQ(decoded->application_id, 4u);
    EXPECT_EQ(decoded->hop_by_hop_id, 42u);
    EXPECT_EQ(decoded->end_to_end_id, 99u);
    EXPECT_EQ(offset, 20u);
    EXPECT_EQ(avps_length, 12u);
}

TEST(DiameterHeader, RejectsTooShortBuffer) {
    std::vector<std::uint8_t> bytes(10, 0);
    std::size_t offset = 0;
    std::uint32_t avps_length = 0;
    EXPECT_FALSE(diameter_core::decode_header(bytes, offset, avps_length).has_value());
}

TEST(DiameterHeader, RejectsWrongVersion) {
    diameter_core::Header h;
    auto bytes = diameter_core::encode_header(h, 0);
    bytes[0] = 2; // not kDiameterVersion
    std::size_t offset = 0;
    std::uint32_t avps_length = 0;
    EXPECT_FALSE(diameter_core::decode_header(bytes, offset, avps_length).has_value());
}

TEST(DiameterAvp, EncodesOctetStringAvpWithoutVendorId) {
    diameter_core::Avp avp;
    avp.code = diameter_core::dictionary::Avp::kOriginHost;
    avp.flags = diameter_core::AvpFlag::kMandatory;
    avp.data = diameter_core::encode_octet_string("chf.example");

    std::vector<std::uint8_t> out;
    diameter_core::encode_avp(out, avp);

    // header(8) + data(11) = 19, padded to 20
    EXPECT_EQ(out.size(), 20u);
    EXPECT_EQ(out[4], diameter_core::AvpFlag::kMandatory);
    // AVP Length field (octets 5-7) = 19 = 0x000013, NOT counting padding
    EXPECT_EQ(out[5], 0x00);
    EXPECT_EQ(out[6], 0x00);
    EXPECT_EQ(out[7], 0x13);
}

TEST(DiameterAvp, EncodesVendorFlaggedAvpWithVendorId) {
    diameter_core::Avp avp;
    avp.code = 999;
    avp.flags = diameter_core::AvpFlag::kVendor | diameter_core::AvpFlag::kMandatory;
    avp.vendor_id = 10415; // 3GPP's real IANA-assigned enterprise number
    avp.data = diameter_core::encode_unsigned32(7);

    std::vector<std::uint8_t> out;
    diameter_core::encode_avp(out, avp);

    // header(12) + data(4) = 16, already 4-aligned
    ASSERT_EQ(out.size(), 16u);

    const auto decoded = diameter_core::decode_avps(out);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].code, 999u);
    EXPECT_EQ((*decoded)[0].vendor_id, 10415u);
    EXPECT_EQ(diameter_core::decode_unsigned32((*decoded)[0].data), 7u);
}

TEST(DiameterAvp, MultipleAvpsRoundTripWithPadding) {
    std::vector<std::uint8_t> out;

    diameter_core::Avp session_id;
    session_id.code = diameter_core::dictionary::Avp::kSessionId;
    session_id.flags = diameter_core::AvpFlag::kMandatory;
    session_id.data = diameter_core::encode_octet_string("chf;123;1"); // 9 bytes, needs padding
    diameter_core::encode_avp(out, session_id);

    diameter_core::Avp cc_request_type;
    cc_request_type.code = diameter_core::dictionary::Dcc::kCcRequestType;
    cc_request_type.flags = diameter_core::AvpFlag::kMandatory;
    cc_request_type.data =
        diameter_core::encode_integer32(diameter_core::dictionary::Dcc::CcRequestType::kInitial);
    diameter_core::encode_avp(out, cc_request_type);

    const auto decoded = diameter_core::decode_avps(out);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2u);

    EXPECT_EQ((*decoded)[0].code, diameter_core::dictionary::Avp::kSessionId);
    EXPECT_EQ(diameter_core::decode_octet_string((*decoded)[0].data), "chf;123;1");

    EXPECT_EQ((*decoded)[1].code, diameter_core::dictionary::Dcc::kCcRequestType);
    EXPECT_EQ(diameter_core::decode_integer32((*decoded)[1].data),
              diameter_core::dictionary::Dcc::CcRequestType::kInitial);

    const auto* found =
        diameter_core::find_avp(*decoded, diameter_core::dictionary::Dcc::kCcRequestType);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(diameter_core::decode_integer32(found->data), 1);

    EXPECT_EQ(diameter_core::find_avp(*decoded, /*code=*/99999), nullptr);
}

TEST(DiameterAvp, GroupedAvpDataDecodesAsNestedAvps) {
    // Requested-Service-Unit (Grouped) containing a real CC-Total-Octets child, matching RFC 4006's
    // real DCC grouped-AVP shape (dict_dcca.c's own Requested-Service-Unit rule set).
    std::vector<std::uint8_t> inner;
    diameter_core::Avp total_octets;
    total_octets.code = diameter_core::dictionary::Dcc::kCcTotalOctets;
    total_octets.flags = diameter_core::AvpFlag::kMandatory;
    total_octets.data = diameter_core::encode_unsigned64(1'000'000);
    diameter_core::encode_avp(inner, total_octets);

    diameter_core::Avp rsu;
    rsu.code = diameter_core::dictionary::Dcc::kRequestedServiceUnit;
    rsu.flags = diameter_core::AvpFlag::kMandatory;
    rsu.data = inner;

    std::vector<std::uint8_t> out;
    diameter_core::encode_avp(out, rsu);

    const auto decoded = diameter_core::decode_avps(out);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].code, diameter_core::dictionary::Dcc::kRequestedServiceUnit);

    const auto nested = diameter_core::decode_avps((*decoded)[0].data);
    ASSERT_TRUE(nested.has_value());
    ASSERT_EQ(nested->size(), 1u);
    EXPECT_EQ((*nested)[0].code, diameter_core::dictionary::Dcc::kCcTotalOctets);
    EXPECT_EQ(diameter_core::decode_unsigned64((*nested)[0].data), 1'000'000u);
}

TEST(DiameterAvp, DecodeRejectsAvpLengthPastEndOfBuffer) {
    std::vector<std::uint8_t> bytes = {
        0, 0, 1, 8, 0x40, 0, 0, 0xFF}; // AVP Length=255, only 8 present
    EXPECT_FALSE(diameter_core::decode_avps(bytes).has_value());
}

TEST(DiameterAvp, EmptyBufferDecodesToZeroAvps) {
    const auto decoded = diameter_core::decode_avps({});
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->empty());
}

TEST(DiameterAvp, AddressIpv4RoundTrips) {
    const std::uint32_t ipv4 = (127u << 24) | (0u << 16) | (0u << 8) | 1u; // 127.0.0.1
    const auto data = diameter_core::encode_address_ipv4(ipv4);
    ASSERT_EQ(data.size(), 6u);
    EXPECT_EQ(data[0], 0x00);
    EXPECT_EQ(data[1], 0x01); // AddressType 1 = IPv4

    const auto decoded = diameter_core::decode_address_ipv4(data);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, ipv4);
}

TEST(DiameterAvp, AddressIpv4RejectsWrongFamily) {
    std::vector<std::uint8_t> data = {0x00, 0x02, 0, 0, 0, 0}; // AddressType 2 = IPv6
    EXPECT_FALSE(diameter_core::decode_address_ipv4(data).has_value());
}
