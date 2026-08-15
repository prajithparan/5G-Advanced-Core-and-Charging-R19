// Unit tests for libs/tcap-core -- byte layouts cross-checked against RestComm jss7's own real,
// cited ITU-T Q.773 tag constants and encode/decode logic (arms-length reference only -- see
// tcap_core/component.hpp's/message.hpp's/dialogue_portion.hpp's own comments and
// docs/DECISIONS.md's Stage 5b ADR update for the full disclosure).

#include "tcap_core/ber.hpp"
#include "tcap_core/component.hpp"
#include "tcap_core/dialogue_portion.hpp"
#include "tcap_core/message.hpp"

#include <gtest/gtest.h>

TEST(TcapBer, IntegerRoundTripsSmallPositive) {
    const auto bytes = tcap_core::encode_integer(5);
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x05);
    EXPECT_EQ(tcap_core::decode_integer(bytes).value_or(-999), 5);
}

TEST(TcapBer, IntegerRoundTripsNegative) {
    const auto bytes = tcap_core::encode_integer(-2);
    EXPECT_EQ(tcap_core::decode_integer(bytes).value_or(999), -2);
}

TEST(TcapBer, IntegerMinimalEncodingForLargeValue) {
    const auto bytes =
        tcap_core::encode_integer(128); // needs 2 bytes (0x00, 0x80) to stay positive
    ASSERT_EQ(bytes.size(), 2u);
    EXPECT_EQ(bytes[0], 0x00);
    EXPECT_EQ(bytes[1], 0x80);
    EXPECT_EQ(tcap_core::decode_integer(bytes).value_or(-1), 128);
}

TEST(TcapBer, OidRoundTrips) {
    // Real Q.773 structured dialogue-as-id: 0.0.17.773.1.1.1
    const std::vector<std::uint32_t> arcs = {0, 0, 17, 773, 1, 1, 1};
    const auto bytes = tcap_core::encode_oid(arcs);
    const auto decoded = tcap_core::decode_oid(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, arcs);
}

TEST(TcapBer, TlvRoundTripsShortForm) {
    tcap_core::Tlv tlv;
    tlv.tag_class = tcap_core::TagClass::kContext;
    tlv.constructed = true;
    tlv.tag_number = 1;
    tlv.value = {0x01, 0x02, 0x03};

    std::vector<std::uint8_t> out;
    tcap_core::encode_tlv(out, tlv);
    ASSERT_EQ(out.size(), 5u); // tag(1) + length(1) + value(3)
    EXPECT_EQ(out[0], 0xA1);   // context(0x80) | constructed(0x20) | tag(1)
    EXPECT_EQ(out[1], 0x03);

    std::size_t offset = 0;
    const auto decoded = tcap_core::decode_tlv(out, offset);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->tag_class, tcap_core::TagClass::kContext);
    EXPECT_TRUE(decoded->constructed);
    EXPECT_EQ(decoded->tag_number, 1u);
    EXPECT_EQ(decoded->value, tlv.value);
    EXPECT_EQ(offset, out.size());
}

TEST(TcapBer, TlvRoundTripsLongFormLength) {
    tcap_core::Tlv tlv;
    tlv.tag_class = tcap_core::TagClass::kUniversal;
    tlv.constructed = false;
    tlv.tag_number = tcap_core::UniversalTag::kOctetString;
    tlv.value.assign(200, 0xAB); // >127 -> real BER long-form length

    std::vector<std::uint8_t> out;
    tcap_core::encode_tlv(out, tlv);
    EXPECT_EQ(out[1], 0x81); // long form, 1 length byte follows
    EXPECT_EQ(out[2], 200);

    std::size_t offset = 0;
    const auto decoded = tcap_core::decode_tlv(out, offset);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value.size(), 200u);
}

TEST(TcapBer, TlvRoundTripsHighTagNumberForm) {
    tcap_core::Tlv tlv;
    tlv.tag_class = tcap_core::TagClass::kContext;
    tlv.constructed = true;
    tlv.tag_number = 30; // real UserInformation tag -- right at the multi-byte-form boundary
    tlv.value = {0x01};

    std::vector<std::uint8_t> out;
    tcap_core::encode_tlv(out, tlv);
    EXPECT_EQ(out[0], 0xBE); // context|constructed|30 fits in single low-tag-number-form byte

    std::size_t offset = 0;
    const auto decoded = tcap_core::decode_tlv(out, offset);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->tag_number, 30u);
}

TEST(TcapBer, RejectsTlvLengthPastEndOfBuffer) {
    std::vector<std::uint8_t> bytes = {0xA1, 0x7F}; // Length=127 but no value bytes follow
    std::size_t offset = 0;
    EXPECT_FALSE(tcap_core::decode_tlv(bytes, offset).has_value());
}

TEST(TcapComponent, InvokeRoundTripsWithLocalOperationCode) {
    tcap_core::Invoke invoke;
    invoke.invoke_id = 1;
    invoke.operation_code.local = 45;                  // arbitrary real-shaped local operation code
    invoke.parameter = {0x30, 0x03, 0x01, 0x01, 0xFF}; // opaque SEQUENCE{BOOLEAN true}

    const auto tlv = tcap_core::encode_invoke(invoke);
    EXPECT_EQ(tlv.tag_class, tcap_core::TagClass::kContext);
    EXPECT_TRUE(tlv.constructed);
    EXPECT_EQ(tlv.tag_number, tcap_core::ComponentTag::kInvoke);

    const auto decoded = tcap_core::decode_component(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->invoke.has_value());
    EXPECT_EQ(decoded->invoke->invoke_id, 1);
    ASSERT_TRUE(decoded->invoke->operation_code.local.has_value());
    EXPECT_EQ(*decoded->invoke->operation_code.local, 45);
    EXPECT_EQ(decoded->invoke->parameter, invoke.parameter);
}

TEST(TcapComponent, InvokeRoundTripsWithLinkedIdAndGlobalOperationCode) {
    tcap_core::Invoke invoke;
    invoke.invoke_id = 3;
    invoke.linked_id = 1;
    invoke.operation_code.global = {1, 2, 3, 4};
    invoke.parameter = {};

    const auto tlv = tcap_core::encode_invoke(invoke);
    const auto decoded = tcap_core::decode_component(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->invoke.has_value());
    ASSERT_TRUE(decoded->invoke->linked_id.has_value());
    EXPECT_EQ(*decoded->invoke->linked_id, 1);
    ASSERT_TRUE(decoded->invoke->operation_code.global.has_value());
    EXPECT_EQ(*decoded->invoke->operation_code.global, (std::vector<std::uint32_t>{1, 2, 3, 4}));
}

TEST(TcapComponent, ReturnResultLastRoundTripsWithResult) {
    tcap_core::ReturnResult rr;
    rr.invoke_id = 2;
    tcap_core::ReturnResult::Result result;
    result.operation_code.local = 45;
    result.parameter = {0x02, 0x01, 0x00};
    rr.result = result;

    const auto tlv = tcap_core::encode_return_result(rr, /*is_last=*/true);
    EXPECT_EQ(tlv.tag_number, tcap_core::ComponentTag::kReturnResultLast);

    const auto decoded = tcap_core::decode_component(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->return_result_last.has_value());
    EXPECT_EQ(decoded->return_result_last->invoke_id, 2);
    ASSERT_TRUE(decoded->return_result_last->result.has_value());
    EXPECT_EQ(*decoded->return_result_last->result->operation_code.local, 45);
    EXPECT_EQ(decoded->return_result_last->result->parameter, result.parameter);
}

TEST(TcapComponent, ReturnResultNotLastRoundTripsWithNoResult) {
    tcap_core::ReturnResult rr;
    rr.invoke_id = 7;
    // real Q.773: the result sequence itself is optional

    const auto tlv = tcap_core::encode_return_result(rr, /*is_last=*/false);
    EXPECT_EQ(tlv.tag_number, tcap_core::ComponentTag::kReturnResult);

    const auto decoded = tcap_core::decode_component(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->return_result.has_value());
    EXPECT_EQ(decoded->return_result->invoke_id, 7);
    EXPECT_FALSE(decoded->return_result->result.has_value());
}

TEST(TcapComponent, ReturnErrorRoundTrips) {
    tcap_core::ReturnError re;
    re.invoke_id = 4;
    re.error_code.local = 34; // arbitrary real-shaped local error code
    re.parameter = {0x02, 0x01, 0x01};

    const auto tlv = tcap_core::encode_return_error(re);
    const auto decoded = tcap_core::decode_component(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->return_error.has_value());
    EXPECT_EQ(decoded->return_error->invoke_id, 4);
    EXPECT_EQ(*decoded->return_error->error_code.local, 34);
}

TEST(TcapComponent, RejectRoundTripsWithInvokeId) {
    tcap_core::Reject rej;
    rej.invoke_id_present = true;
    rej.invoke_id = 9;
    rej.problem_choice_tag = 1; // real ProblemType.Invoke
    rej.problem_value = 2;

    const auto tlv = tcap_core::encode_reject(rej);
    const auto decoded = tcap_core::decode_component(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->reject.has_value());
    EXPECT_TRUE(decoded->reject->invoke_id_present);
    EXPECT_EQ(decoded->reject->invoke_id, 9);
    EXPECT_EQ(decoded->reject->problem_choice_tag, 1);
    EXPECT_EQ(decoded->reject->problem_value, 2);
}

TEST(TcapComponent, RejectRoundTripsWithoutInvokeId) {
    tcap_core::Reject rej;
    rej.invoke_id_present = false;
    rej.problem_choice_tag = 0; // real ProblemType.General
    rej.problem_value = 1;

    const auto tlv = tcap_core::encode_reject(rej);
    const auto decoded = tcap_core::decode_component(tlv);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->reject.has_value());
    EXPECT_FALSE(decoded->reject->invoke_id_present);
}

TEST(TcapMessage, TcBeginRoundTripsWithComponent) {
    tcap_core::TcBegin msg;
    msg.originating_transaction_id = {0x00, 0x00, 0x00, 0x01};

    tcap_core::Invoke invoke;
    invoke.invoke_id = 1;
    invoke.operation_code.local = 45;
    invoke.parameter = {};
    msg.components.push_back(tcap_core::encode_invoke(invoke));

    const auto bytes = tcap_core::encode_tc_begin(msg);
    EXPECT_EQ(tcap_core::peek_tc_message_tag(bytes).value_or(0), tcap_core::MessageTag::kBegin);

    const auto decoded = tcap_core::decode_tc_begin(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->originating_transaction_id, msg.originating_transaction_id);
    ASSERT_EQ(decoded->components.size(), 1u);

    const auto component = tcap_core::decode_component(decoded->components[0]);
    ASSERT_TRUE(component.has_value());
    ASSERT_TRUE(component->invoke.has_value());
    EXPECT_EQ(component->invoke->invoke_id, 1);
}

TEST(TcapMessage, TcContinueRoundTrips) {
    tcap_core::TcContinue msg;
    msg.originating_transaction_id = {0x00, 0x00, 0x00, 0x01};
    msg.destination_transaction_id = {0x00, 0x00, 0x00, 0x02};

    const auto bytes = tcap_core::encode_tc_continue(msg);
    EXPECT_EQ(tcap_core::peek_tc_message_tag(bytes).value_or(0), tcap_core::MessageTag::kContinue);

    const auto decoded = tcap_core::decode_tc_continue(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->originating_transaction_id, msg.originating_transaction_id);
    EXPECT_EQ(decoded->destination_transaction_id, msg.destination_transaction_id);
}

TEST(TcapMessage, TcEndRoundTrips) {
    tcap_core::TcEnd msg;
    msg.destination_transaction_id = {0x00, 0x00, 0x00, 0x02};

    const auto bytes = tcap_core::encode_tc_end(msg);
    EXPECT_EQ(tcap_core::peek_tc_message_tag(bytes).value_or(0), tcap_core::MessageTag::kEnd);

    const auto decoded = tcap_core::decode_tc_end(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->destination_transaction_id, msg.destination_transaction_id);
}

TEST(TcapMessage, TcAbortRoundTripsWithPAbortCause) {
    tcap_core::TcAbort msg;
    msg.destination_transaction_id = {0x00, 0x00, 0x00, 0x02};
    msg.p_abort_cause = 1; // real PAbortCauseType-shaped value

    const auto bytes = tcap_core::encode_tc_abort(msg);
    EXPECT_EQ(tcap_core::peek_tc_message_tag(bytes).value_or(0), tcap_core::MessageTag::kAbort);

    const auto decoded = tcap_core::decode_tc_abort(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->p_abort_cause.has_value());
    EXPECT_EQ(*decoded->p_abort_cause, 1);
    EXPECT_FALSE(decoded->dialogue_portion.has_value());
}

TEST(TcapMessage, TcUniRoundTrips) {
    tcap_core::TcUni msg;
    tcap_core::Invoke invoke;
    invoke.invoke_id = 1;
    invoke.operation_code.local = 10;
    msg.components.push_back(tcap_core::encode_invoke(invoke));

    const auto bytes = tcap_core::encode_tc_uni(msg);
    EXPECT_EQ(tcap_core::peek_tc_message_tag(bytes).value_or(0), tcap_core::MessageTag::kUni);

    const auto decoded = tcap_core::decode_tc_uni(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->components.size(), 1u);
}

TEST(TcapMessage, RejectsMismatchedMessageType) {
    tcap_core::TcEnd msg;
    msg.destination_transaction_id = {0x01};
    const auto bytes = tcap_core::encode_tc_end(msg);

    EXPECT_FALSE(tcap_core::decode_tc_begin(bytes).has_value());
}

TEST(TcapDialoguePortion, RoundTripsAarqWithApplicationContextName) {
    tcap_core::DialogueRequest req;
    req.application_context_name = {0, 4, 0, 0, 1, 0, 21, 3}; // arbitrary real-shaped OID
    req.user_information = {0x28, 0x02, 0x01, 0x00};          // opaque, arbitrary

    const auto bytes = tcap_core::encode_dialogue_portion_request(req);
    const auto decoded = tcap_core::decode_dialogue_portion_request(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->application_context_name, req.application_context_name);
    ASSERT_TRUE(decoded->user_information.has_value());
    EXPECT_EQ(*decoded->user_information, *req.user_information);
    EXPECT_FALSE(decoded->protocol_version.has_value());
}

TEST(TcapDialoguePortion, RoundTripsWithinTcBegin) {
    tcap_core::DialogueRequest req;
    req.application_context_name = {0, 4, 0, 0, 1, 0, 21, 3};

    tcap_core::TcBegin msg;
    msg.originating_transaction_id = {0x00, 0x00, 0x00, 0x01};
    msg.dialogue_portion = tcap_core::encode_dialogue_portion_request(req);

    const auto bytes = tcap_core::encode_tc_begin(msg);
    const auto decoded = tcap_core::decode_tc_begin(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->dialogue_portion.has_value());

    const auto decoded_req = tcap_core::decode_dialogue_portion_request(*decoded->dialogue_portion);
    ASSERT_TRUE(decoded_req.has_value());
    EXPECT_EQ(decoded_req->application_context_name, req.application_context_name);
}

TEST(TcapDialoguePortion, RoundTripsAareAcceptedWithUserDiagnostic) {
    tcap_core::DialogueResponse res;
    res.application_context_name = {0, 4, 0, 0, 1, 0, 21, 3};
    res.result = tcap_core::ResultType::kAccepted;
    res.diagnostic.is_user_type = true;
    res.diagnostic.value = tcap_core::DialogServiceUserType::kNull;
    res.user_information = {0x28, 0x02, 0x01, 0x00};

    const auto bytes = tcap_core::encode_dialogue_portion_response(res);
    const auto decoded = tcap_core::decode_dialogue_portion_response(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->application_context_name, res.application_context_name);
    EXPECT_EQ(decoded->result, tcap_core::ResultType::kAccepted);
    EXPECT_TRUE(decoded->diagnostic.is_user_type);
    EXPECT_EQ(decoded->diagnostic.value, tcap_core::DialogServiceUserType::kNull);
    ASSERT_TRUE(decoded->user_information.has_value());
    EXPECT_EQ(*decoded->user_information, *res.user_information);
    EXPECT_FALSE(decoded->protocol_version.has_value());
}

TEST(TcapDialoguePortion, RoundTripsAareRejectedWithProviderDiagnostic) {
    tcap_core::DialogueResponse res;
    res.application_context_name = {0, 4, 0, 0, 1, 0, 21, 3};
    res.result = tcap_core::ResultType::kRejectedPermanent;
    res.diagnostic.is_user_type = false;
    res.diagnostic.value = tcap_core::DialogServiceProviderType::kNoCommonDialogPortion;

    const auto bytes = tcap_core::encode_dialogue_portion_response(res);
    const auto decoded = tcap_core::decode_dialogue_portion_response(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->result, tcap_core::ResultType::kRejectedPermanent);
    EXPECT_FALSE(decoded->diagnostic.is_user_type);
    EXPECT_EQ(decoded->diagnostic.value,
              tcap_core::DialogServiceProviderType::kNoCommonDialogPortion);
    EXPECT_FALSE(decoded->user_information.has_value());
}

TEST(TcapDialoguePortion, DecodeResponseRejectsRealAarq) {
    tcap_core::DialogueRequest req;
    req.application_context_name = {0, 4, 0, 0, 1, 0, 21, 3};
    const auto bytes = tcap_core::encode_dialogue_portion_request(req);
    EXPECT_FALSE(tcap_core::decode_dialogue_portion_response(bytes).has_value());
}

TEST(TcapDialoguePortion, DecodeRequestRejectsRealAare) {
    tcap_core::DialogueResponse res;
    res.application_context_name = {0, 4, 0, 0, 1, 0, 21, 3};
    res.result = tcap_core::ResultType::kAccepted;
    const auto bytes = tcap_core::encode_dialogue_portion_response(res);
    EXPECT_FALSE(tcap_core::decode_dialogue_portion_request(bytes).has_value());
}

TEST(TcapDialoguePortion, AareRoundTripsWithinTcEnd) {
    tcap_core::DialogueResponse res;
    res.application_context_name = {0, 4, 0, 0, 1, 0, 21, 3};
    res.result = tcap_core::ResultType::kAccepted;
    res.diagnostic.is_user_type = true;
    res.diagnostic.value = tcap_core::DialogServiceUserType::kNull;

    tcap_core::TcEnd msg;
    msg.destination_transaction_id = {0x00, 0x00, 0x00, 0x01};
    msg.dialogue_portion = tcap_core::encode_dialogue_portion_response(res);

    const auto bytes = tcap_core::encode_tc_end(msg);
    const auto decoded = tcap_core::decode_tc_end(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->dialogue_portion.has_value());

    const auto decoded_res =
        tcap_core::decode_dialogue_portion_response(*decoded->dialogue_portion);
    ASSERT_TRUE(decoded_res.has_value());
    EXPECT_EQ(decoded_res->result, tcap_core::ResultType::kAccepted);
}
