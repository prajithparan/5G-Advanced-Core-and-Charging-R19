// ADR-0268 regression: the open-type presence index asn1c 0.9.29 writes on a decoded NGAP PDU is
// wrong, and ngap::decode_pdu now repairs it.
//
// asn1c's generated selector (select_<Outer>_value_type) ends with `result.presence_index = row +
// 1`, where `row` indexes the ALL-procedures Information Object Set table -- not the `value`
// CHOICE, which only holds the procedures that have that outcome. A decoded
// HandoverPreparationFailure therefore came back claiming to be an InitialContextSetupFailure
// (present 8 instead of 5). The decoded bytes land in the right place either way, so nothing
// visibly misbehaved; what broke was the free, which walked the union as the wrong message type
// and leaked every IE's value buffer. See docs/DECISIONS.md ADR-0268.
//
// Two independent things are asserted here, and both matter:
//   1. `value.present` -- for the unsuccessful-outcome case this fails without the repair in any
//      build (asn1c returns 8, InitialContextSetupFailure). Stated rather than implied: it does
//      NOT fail for the other two, because asn1c's index happens to be right for every initiating
//      message (0 of 76 wrong) and every successful outcome (0 of 29); only unsuccessful outcomes
//      are mis-indexed (13 of 15). Those two cases are regression guards for the repair itself --
//      it must not disturb the indices that were already correct -- not reproducers.
//   2. the ASN_STRUCT_FREE at the end of each case -- only fails under ASan (CI's
//      `sanitize (asan-ubsan)` leg), which is exactly how the defect stayed invisible for so long.
//
// No NF process is involved: this is pure codec, encode -> decode -> free.

#include <cstdlib>
#include <vector>

#include "ngap_core/ngap_codec.hpp"

#include <gtest/gtest.h>

extern "C" {
#include <AMF-UE-NGAP-ID.h>
#include <Cause.h>
#include <HandoverPreparationFailure.h>
#include <InitialUEMessage.h>
#include <InitiatingMessage.h>
#include <NGSetupResponse.h>
#include <RAN-UE-NGAP-ID.h>
#include <SuccessfulOutcome.h>
#include <UnsuccessfulOutcome.h>
}

namespace {

constexpr long kIdAmfUeNgapId = 10;
constexpr long kIdRanUeNgapId = 85;
constexpr long kIdCause = 15;

constexpr unsigned long kAmfUeId = 42;
constexpr long kRanUeId = 7;

// Fills a ConcreteProtocolIE-Container with the three IEs every message under test can carry,
// each PER-encoded through its own real type descriptor (the ADR-0031 open-type workaround).
void add_common_ies(ConcreteProtocolIE_Container_t& container) {
    AMF_UE_NGAP_ID_t amf_ue_id{};
    asn_ulong2INTEGER(&amf_ue_id, kAmfUeId);
    ngap::add_ie(
        container,
        ngap::make_ie(kIdAmfUeNgapId, Criticality_ignore, &asn_DEF_AMF_UE_NGAP_ID, &amf_ue_id));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_AMF_UE_NGAP_ID, &amf_ue_id);

    RAN_UE_NGAP_ID_t ran_ue_id = kRanUeId;
    ngap::add_ie(
        container,
        ngap::make_ie(kIdRanUeNgapId, Criticality_ignore, &asn_DEF_RAN_UE_NGAP_ID, &ran_ue_id));

    Cause_t cause{};
    cause.present = Cause_PR_radioNetwork;
    cause.choice.radioNetwork = CauseRadioNetwork_unspecified;
    ngap::add_ie(container, ngap::make_ie(kIdCause, Criticality_ignore, &asn_DEF_Cause, &cause));
}

// Asserts the decoded container still carries the IEs that were encoded -- the repair rewrites a
// presence tag, so it has to be shown not to disturb the payload it points at.
void expect_common_ies(const ConcreteProtocolIE_Container_t& container) {
    const ConcreteProtocolIE_Field_t* ran_ue_ie = ngap::find_ie(container, kIdRanUeNgapId);
    ASSERT_NE(ran_ue_ie, nullptr) << "id-RAN-UE-NGAP-ID missing from the decoded container";
    auto* ran_ue_id =
        static_cast<RAN_UE_NGAP_ID_t*>(ngap::decode_ie_value(&asn_DEF_RAN_UE_NGAP_ID, *ran_ue_ie));
    ASSERT_NE(ran_ue_id, nullptr);
    EXPECT_EQ(*ran_ue_id, kRanUeId);
    ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, ran_ue_id);

    const ConcreteProtocolIE_Field_t* cause_ie = ngap::find_ie(container, kIdCause);
    ASSERT_NE(cause_ie, nullptr) << "id-Cause missing from the decoded container";
    auto* cause = static_cast<Cause_t*>(ngap::decode_ie_value(&asn_DEF_Cause, *cause_ie));
    ASSERT_NE(cause, nullptr);
    EXPECT_EQ(cause->present, Cause_PR_radioNetwork);
    ASN_STRUCT_FREE(asn_DEF_Cause, cause);
}

} // namespace

// UnsuccessfulOutcome: the case the defect was found on. Correct presence 5; asn1c produced 8
// (InitialContextSetupFailure), whose ProtocolIE-Container is the parameterized kind -- freeing
// one shape as the other is what leaked.
TEST(NgapCodecPresence, UnsuccessfulOutcomeCarriesItsOwnPresenceIndex) {
    HandoverPreparationFailure_t fail{};
    add_common_ies(fail.protocolIEs);

    NGAP_PDU_t pdu{};
    pdu.present = NGAP_PDU_PR_unsuccessfulOutcome;
    pdu.choice.unsuccessfulOutcome =
        static_cast<UnsuccessfulOutcome_t*>(std::calloc(1, sizeof(UnsuccessfulOutcome_t)));
    ASSERT_NE(pdu.choice.unsuccessfulOutcome, nullptr);
    pdu.choice.unsuccessfulOutcome->procedureCode = 12 /* id-HandoverPreparation */;
    pdu.choice.unsuccessfulOutcome->criticality = Criticality_reject;
    pdu.choice.unsuccessfulOutcome->value.present =
        UnsuccessfulOutcome__value_PR_HandoverPreparationFailure;
    pdu.choice.unsuccessfulOutcome->value.choice.HandoverPreparationFailure = fail;

    const std::vector<std::uint8_t> bytes = ngap::encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &pdu);
    ASSERT_FALSE(bytes.empty());

    NGAP_PDU_t* decoded = ngap::decode_pdu(bytes);
    ASSERT_NE(decoded, nullptr);
    ASSERT_EQ(decoded->present, NGAP_PDU_PR_unsuccessfulOutcome);
    EXPECT_EQ(decoded->choice.unsuccessfulOutcome->value.present,
              UnsuccessfulOutcome__value_PR_HandoverPreparationFailure);
    expect_common_ies(
        decoded->choice.unsuccessfulOutcome->value.choice.HandoverPreparationFailure.protocolIEs);
    ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, decoded);
}

// InitiatingMessage: the outcome AMF decodes on literally every uplink PDU from a gNB.
TEST(NgapCodecPresence, InitiatingMessageCarriesItsOwnPresenceIndex) {
    InitialUEMessage_t initial{};
    add_common_ies(initial.protocolIEs);

    NGAP_PDU_t pdu{};
    pdu.present = NGAP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage =
        static_cast<InitiatingMessage_t*>(std::calloc(1, sizeof(InitiatingMessage_t)));
    ASSERT_NE(pdu.choice.initiatingMessage, nullptr);
    pdu.choice.initiatingMessage->procedureCode = 15 /* id-InitialUEMessage */;
    pdu.choice.initiatingMessage->criticality = Criticality_ignore;
    pdu.choice.initiatingMessage->value.present = InitiatingMessage__value_PR_InitialUEMessage;
    pdu.choice.initiatingMessage->value.choice.InitialUEMessage = initial;

    const std::vector<std::uint8_t> bytes = ngap::encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &pdu);
    ASSERT_FALSE(bytes.empty());

    NGAP_PDU_t* decoded = ngap::decode_pdu(bytes);
    ASSERT_NE(decoded, nullptr);
    ASSERT_EQ(decoded->present, NGAP_PDU_PR_initiatingMessage);
    EXPECT_EQ(decoded->choice.initiatingMessage->value.present,
              InitiatingMessage__value_PR_InitialUEMessage);
    expect_common_ies(decoded->choice.initiatingMessage->value.choice.InitialUEMessage.protocolIEs);
    ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, decoded);
}

// SuccessfulOutcome: the outcome ADR-0264's test gNB decodes coming back from AMF.
TEST(NgapCodecPresence, SuccessfulOutcomeCarriesItsOwnPresenceIndex) {
    NGSetupResponse_t response{};
    add_common_ies(response.protocolIEs);

    NGAP_PDU_t pdu{};
    pdu.present = NGAP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome =
        static_cast<SuccessfulOutcome_t*>(std::calloc(1, sizeof(SuccessfulOutcome_t)));
    ASSERT_NE(pdu.choice.successfulOutcome, nullptr);
    pdu.choice.successfulOutcome->procedureCode = 21 /* id-NGSetup */;
    pdu.choice.successfulOutcome->criticality = Criticality_reject;
    pdu.choice.successfulOutcome->value.present = SuccessfulOutcome__value_PR_NGSetupResponse;
    pdu.choice.successfulOutcome->value.choice.NGSetupResponse = response;

    const std::vector<std::uint8_t> bytes = ngap::encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &pdu);
    ASSERT_FALSE(bytes.empty());

    NGAP_PDU_t* decoded = ngap::decode_pdu(bytes);
    ASSERT_NE(decoded, nullptr);
    ASSERT_EQ(decoded->present, NGAP_PDU_PR_successfulOutcome);
    EXPECT_EQ(decoded->choice.successfulOutcome->value.present,
              SuccessfulOutcome__value_PR_NGSetupResponse);
    expect_common_ies(decoded->choice.successfulOutcome->value.choice.NGSetupResponse.protocolIEs);
    ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, decoded);
}
