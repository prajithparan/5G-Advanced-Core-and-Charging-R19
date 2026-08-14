// Integration test proving libs/tcap-core and libs/ss7-core compose correctly end-to-end -- a
// real TCAP message travels inside a real SCCP UDT `data` field, which travels inside a real M3UA
// DATA message's Protocol Data parameter, exactly the way a real Gy-analogous Sy-analogous MAP/CAP
// dialogue would be carried in a real deployment (TCAP/SCCP/M3UA, P4.5/ADR-0059 Stage 5a+5b). Real,
// disclosed scope: this proves the codecs compose (plain byte-vector handoff between layers, no
// glue code needed since each layer's own `data`/`value`/`user_protocol_data` field is already
// just `std::vector<std::uint8_t>`) -- it is NOT a real SCTP transport test (no network listener
// exists yet for this stack) and NOT a real MAP/CAP operation (the Invoke's own `parameter` bytes
// here are an arbitrary opaque placeholder, since no real MAP/CAP ASN.1 argument type is built
// yet, Stage 5b's own disclosed remaining gap).

#include "ss7_core/m3ua_dictionary.hpp"
#include "ss7_core/m3ua_header.hpp"
#include "ss7_core/m3ua_protocol_data.hpp"
#include "ss7_core/m3ua_tlv.hpp"
#include "ss7_core/sccp_address.hpp"
#include "ss7_core/sccp_dictionary.hpp"
#include "ss7_core/sccp_udt.hpp"
#include "tcap_core/component.hpp"
#include "tcap_core/message.hpp"

#include <gtest/gtest.h>

TEST(Ss7TcapStack, TcapBeginTravelsInsideScpUdtInsideM3uaData) {
    // --- Layer 1: build a real TCAP TC-Begin carrying one real Invoke component. ---
    tcap_core::Invoke invoke;
    invoke.invoke_id = 1;
    invoke.operation_code.local = 45; // arbitrary real-shaped local operation code (opaque here --
                                      // no real MAP/CAP operation-code registry exists yet)
    invoke.parameter = {0x04, 0x03, 'S', 'S', '7'}; // opaque OCTET STRING placeholder

    tcap_core::TcBegin begin;
    begin.originating_transaction_id = {0x00, 0x00, 0x00, 0x2A};
    begin.components.push_back(tcap_core::encode_invoke(invoke));

    const auto tcap_bytes = tcap_core::encode_tc_begin(begin);
    EXPECT_EQ(tcap_core::peek_tc_message_tag(tcap_bytes).value_or(0),
              tcap_core::MessageTag::kBegin);

    // --- Layer 2: wrap the TCAP message in a real SCCP UDT, addressed by real SSN. ---
    ss7_core::SccpUdt udt;
    udt.protocol_class = ss7_core::dictionary::ProtocolClass::kClass0;
    udt.called_party.ssn_present = true;
    udt.called_party.ssn = ss7_core::dictionary::SubsystemNumber::kHlr;
    udt.calling_party.ssn_present = true;
    udt.calling_party.ssn = ss7_core::dictionary::SubsystemNumber::kMsc;
    udt.data = tcap_bytes; // plain byte handoff -- SCCP itself doesn't interpret TCAP's content

    const auto sccp_bytes = ss7_core::encode_sccp_udt(udt);
    EXPECT_EQ(sccp_bytes[0], ss7_core::dictionary::MessageType::kUdt);

    // --- Layer 3: wrap the SCCP message in a real M3UA Protocol Data parameter (SI=SCCP). ---
    ss7_core::M3uaProtocolData proto_data;
    proto_data.opc = 1001;
    proto_data.dpc = 2002;
    proto_data.si = ss7_core::dictionary::ServiceIndicator::kSccp;
    proto_data.ni = 2;
    proto_data.sls = 3;
    proto_data.user_protocol_data = sccp_bytes; // plain byte handoff again

    const auto proto_data_bytes = ss7_core::encode_m3ua_protocol_data(proto_data);

    // --- Layer 4: real M3UA DATA message (header + Protocol Data TLV). ---
    ss7_core::M3uaTlv protocol_data_tlv;
    protocol_data_tlv.tag = ss7_core::dictionary::ParamTag::kProtocolData;
    protocol_data_tlv.value = proto_data_bytes;

    std::vector<std::uint8_t> tlv_bytes;
    ss7_core::encode_m3ua_tlv(tlv_bytes, protocol_data_tlv);

    ss7_core::M3uaHeader header;
    header.message_class = ss7_core::dictionary::MessageClass::kTransfer;
    header.message_type = ss7_core::dictionary::TransferMessageType::kData;
    auto full_message =
        ss7_core::encode_m3ua_header(header, static_cast<std::uint32_t>(tlv_bytes.size()));
    full_message.insert(full_message.end(), tlv_bytes.begin(), tlv_bytes.end());

    // --- Full decode, reversing every layer, back to the original real Invoke. ---
    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    const auto decoded_header = ss7_core::decode_m3ua_header(full_message, offset, payload_length);
    ASSERT_TRUE(decoded_header.has_value());
    EXPECT_EQ(decoded_header->message_class, ss7_core::dictionary::MessageClass::kTransfer);
    EXPECT_EQ(decoded_header->message_type, ss7_core::dictionary::TransferMessageType::kData);

    std::vector<std::uint8_t> payload(full_message.begin() + static_cast<std::ptrdiff_t>(offset),
                                      full_message.end());
    ASSERT_EQ(payload.size(), payload_length);

    const auto decoded_tlvs = ss7_core::decode_m3ua_tlvs(payload);
    ASSERT_TRUE(decoded_tlvs.has_value());
    const auto* decoded_pd_tlv =
        ss7_core::find_m3ua_tlv(*decoded_tlvs, ss7_core::dictionary::ParamTag::kProtocolData);
    ASSERT_NE(decoded_pd_tlv, nullptr);

    const auto decoded_proto_data = ss7_core::decode_m3ua_protocol_data(decoded_pd_tlv->value);
    ASSERT_TRUE(decoded_proto_data.has_value());
    EXPECT_EQ(decoded_proto_data->si, ss7_core::dictionary::ServiceIndicator::kSccp);
    EXPECT_EQ(decoded_proto_data->opc, 1001u);
    EXPECT_EQ(decoded_proto_data->dpc, 2002u);

    const auto decoded_udt = ss7_core::decode_sccp_udt(decoded_proto_data->user_protocol_data);
    ASSERT_TRUE(decoded_udt.has_value());
    EXPECT_TRUE(decoded_udt->called_party.ssn_present);
    EXPECT_EQ(decoded_udt->called_party.ssn, ss7_core::dictionary::SubsystemNumber::kHlr);

    const auto decoded_begin = tcap_core::decode_tc_begin(decoded_udt->data);
    ASSERT_TRUE(decoded_begin.has_value());
    EXPECT_EQ(decoded_begin->originating_transaction_id, begin.originating_transaction_id);
    ASSERT_EQ(decoded_begin->components.size(), 1u);

    const auto decoded_component = tcap_core::decode_component(decoded_begin->components[0]);
    ASSERT_TRUE(decoded_component.has_value());
    ASSERT_TRUE(decoded_component->invoke.has_value());
    EXPECT_EQ(decoded_component->invoke->invoke_id, 1);
    ASSERT_TRUE(decoded_component->invoke->operation_code.local.has_value());
    EXPECT_EQ(*decoded_component->invoke->operation_code.local, 45);
    EXPECT_EQ(decoded_component->invoke->parameter, invoke.parameter);
}
