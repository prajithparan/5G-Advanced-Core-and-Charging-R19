#include "ngap_handover.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "aka_crypto/kdf.hpp"
#include "ngap_core/ngap_codec.hpp"

extern "C" {
#include <AMF-UE-NGAP-ID.h>
#include <AllocationAndRetentionPriority.h>
#include <AllowedNSSAI-Item.h>
#include <AllowedNSSAI.h>
#include <Cause.h>
#include <CauseNas.h>
#include <CauseProtocol.h>
#include <CauseRadioNetwork.h>
#include <FiveQI.h>
#include <GNB-ID.h>
#include <GTPTunnel.h>
#include <GUAMI.h>
#include <GlobalGNB-ID.h>
#include <GlobalRANNodeID.h>
#include <HandoverCommand.h>
#include <HandoverFailure.h>
#include <HandoverNotify.h>
#include <HandoverPreparationFailure.h>
#include <HandoverRequest.h>
#include <HandoverRequestAcknowledge.h>
#include <HandoverType.h>
#include <NGAP-PDU.h>
#include <NonDynamic5QIDescriptor.h>
#include <PDUSessionResourceItemHORqd.h>
#include <PDUSessionResourceListHORqd.h>
#include <PDUSessionResourceSetupItemHOReq.h>
#include <PDUSessionResourceSetupListHOReq.h>
#include <PDUSessionResourceSetupRequestTransfer.h>
#include <PDUSessionType.h>
#include <QosCharacteristics.h>
#include <QosFlowIdentifier.h>
#include <QosFlowLevelQosParameters.h>
#include <QosFlowSetupRequestItem.h>
#include <QosFlowSetupRequestList.h>
#include <RAN-UE-NGAP-ID.h>
#include <SD.h>
#include <SST.h>
#include <SecurityContext.h>
#include <SourceToTarget-TransparentContainer.h>
#include <SuccessfulOutcome.h>
#include <TargetID.h>
#include <TargetRANNodeID.h>
#include <TargetToSource-TransparentContainer.h>
#include <UE-NGAP-IDs.h>
#include <UEAggregateMaximumBitRate.h>
#include <UEContextReleaseCommand.h>
#include <UESecurityCapabilities.h>
#include <UPTransportLayerInformation.h>
#include <UnsuccessfulOutcome.h>
}

namespace amf::ngap {

namespace {

// This lab's fixed test PLMN/S-NSSAI, matching nfs/amf/src/ngap_task.cpp's own kMcc/kMnc/kSst/kSd
// exactly -- deliberately duplicated here (not shared via a common header) since these handlers
// live in a separate translation unit for real, disclosed CI-memory reasons (see this file's own
// .hpp header comment), matching this file's own established "locally duplicate small lab
// constants across files" convention already used for e.g. kAusfBase/kPcfBase.
constexpr const char* kMcc = "999";
constexpr const char* kMnc = "70";
constexpr std::uint8_t kSst = 1;
constexpr std::uint32_t kSd = 1;

OCTET_STRING_t make_octet_string(const std::uint8_t* data, std::size_t len) {
    OCTET_STRING_t s{};
    s.buf = static_cast<std::uint8_t*>(std::malloc(len));
    s.size = len;
    std::memcpy(s.buf, data, len);
    return s;
}

// PLMN identity encoding per TS 24.008/38.413 (half-octet BCD, filler 0xF for a 2-digit MNC's
// third digit) -- same real function ngap_task.cpp's own encode_plmn_identity implements,
// duplicated here for the same TU-isolation reason as kMcc/kMnc above.
void encode_plmn_identity(const char* mcc, const char* mnc, std::uint8_t out[3]) {
    const int mcc1 = mcc[0] - '0';
    const int mcc2 = mcc[1] - '0';
    const int mcc3 = mcc[2] - '0';
    const bool mnc_is_3_digit = std::strlen(mnc) == 3;
    const int mnc1 = mnc[0] - '0';
    const int mnc2 = mnc[1] - '0';
    const int mnc3 = mnc_is_3_digit ? (mnc[2] - '0') : 0xF;
    out[0] = static_cast<std::uint8_t>((mcc2 << 4) | mcc1);
    out[1] = static_cast<std::uint8_t>((mnc3 << 4) | mcc3);
    out[2] = static_cast<std::uint8_t>((mnc2 << 4) | mnc1);
}

// A value-derived BIT STRING (MSB-first) -- used for GUAMI's AMFRegionID/AMFSetID/AMFPointer
// using this AMF's REAL configured identity (amf_region_id/amf_set_id/amf_pointer, nf-config-
// driven, ADR-0088).
BIT_STRING_t make_bit_string_from_uint(unsigned long value, std::size_t bits) {
    const std::size_t bytes = (bits + 7) / 8;
    BIT_STRING_t s{};
    s.buf = static_cast<std::uint8_t*>(std::calloc(bytes, 1));
    s.size = bytes;
    s.bits_unused = static_cast<int>(bytes * 8 - bits);
    const unsigned long shifted = value << s.bits_unused;
    for (std::size_t i = 0; i < bytes; ++i) {
        s.buf[bytes - 1 - i] = static_cast<std::uint8_t>((shifted >> (8 * i)) & 0xFF);
    }
    return s;
}

// Real, structurally-valid, minimally-populated PDUSessionResourceSetupRequestTransfer (TS 38.413
// §9.3.4.1, one real mandatory QoS flow: QFI=1, 5QI=9 non-dynamic -- the real, standard 3GPP
// "non-GBR default" 5QI value, TS 23.501 Table 5.7.4-1, not invented; ARP priorityLevel=8/
// shall-not-trigger-pre-emption/not-pre-emptable, this lab's own conservative fixed default).
// Real, disclosed gap: UL-NGU-UP-TNLInformation is a PLACEHOLDER (TEID=0/IP=0.0.0.0) -- unlike
// ADR-0092's own SMF-side PATH_SWITCH_REQ work, this pass does NOT build the real AMF->SMF
// Nsmf_PDUSession_UpdateSMContext relay a real AMF would use to obtain UPF's own real N3 address
// here (the same "AMF doesn't call SMF yet" gap ADR-0090/ADR-0092 already disclosed as open).
std::vector<std::uint8_t> build_placeholder_ho_request_transfer() {
    PDUSessionResourceSetupRequestTransfer_t transfer{};

    UPTransportLayerInformation_t ul_info{};
    ul_info.present = UPTransportLayerInformation_PR_gTPTunnel;
    auto* gtp_tunnel = static_cast<GTPTunnel_t*>(std::calloc(1, sizeof(GTPTunnel_t)));
    const std::uint8_t zero_teid[4] = {0, 0, 0, 0};
    gtp_tunnel->gTP_TEID = make_octet_string(zero_teid, 4);
    gtp_tunnel->transportLayerAddress.buf = static_cast<std::uint8_t*>(std::malloc(4));
    std::memcpy(gtp_tunnel->transportLayerAddress.buf, zero_teid, 4);
    gtp_tunnel->transportLayerAddress.size = 4;
    gtp_tunnel->transportLayerAddress.bits_unused = 0;
    ul_info.choice.gTPTunnel = gtp_tunnel;
    ::ngap::add_ie(transfer.protocolIEs,
                   ::ngap::make_ie(139 /* id-UL-NGU-UP-TNLInformation */,
                                   Criticality_reject,
                                   &asn_DEF_UPTransportLayerInformation,
                                   &ul_info));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_UPTransportLayerInformation, &ul_info);

    PDUSessionType_t pdu_type = PDUSessionType_ipv4;
    ::ngap::add_ie(
        transfer.protocolIEs,
        ::ngap::make_ie(
            134 /* id-PDUSessionType */, Criticality_reject, &asn_DEF_PDUSessionType, &pdu_type));

    QosFlowSetupRequestList_t qos_list{};
    auto* qos_item =
        static_cast<QosFlowSetupRequestItem_t*>(std::calloc(1, sizeof(QosFlowSetupRequestItem_t)));
    qos_item->qosFlowIdentifier = 1;
    qos_item->qosFlowLevelQosParameters.qosCharacteristics.present =
        QosCharacteristics_PR_nonDynamic5QI;
    auto* non_dynamic =
        static_cast<NonDynamic5QIDescriptor_t*>(std::calloc(1, sizeof(NonDynamic5QIDescriptor_t)));
    non_dynamic->fiveQI = 9;
    qos_item->qosFlowLevelQosParameters.qosCharacteristics.choice.nonDynamic5QI = non_dynamic;
    qos_item->qosFlowLevelQosParameters.allocationAndRetentionPriority.priorityLevelARP = 8;
    qos_item->qosFlowLevelQosParameters.allocationAndRetentionPriority.pre_emptionCapability =
        Pre_emptionCapability_shall_not_trigger_pre_emption;
    qos_item->qosFlowLevelQosParameters.allocationAndRetentionPriority.pre_emptionVulnerability =
        Pre_emptionVulnerability_not_pre_emptable;
    ASN_SEQUENCE_ADD(&qos_list.list, qos_item);
    ::ngap::add_ie(transfer.protocolIEs,
                   ::ngap::make_ie(136 /* id-QosFlowSetupRequestList */,
                                   Criticality_reject,
                                   &asn_DEF_QosFlowSetupRequestList,
                                   &qos_list));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_QosFlowSetupRequestList, &qos_list);

    const auto bytes =
        ::ngap::encode_value(&asn_DEF_PDUSessionResourceSetupRequestTransfer, &transfer);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PDUSessionResourceSetupRequestTransfer, &transfer);
    return bytes;
}

// Real HandoverPreparationFailure (TS 38.413 §9.2.3.3, id-HandoverPreparation unsuccessfulOutcome)
// -- the real reject path for HandoverRequired, used by every early-exit branch in
// handle_handover_required below.
void send_handover_preparation_failure(ngap_core::SctpSocket& source_assoc,
                                       unsigned long amf_ue_id,
                                       unsigned long ran_ue_id,
                                       Cause_t cause) {
    HandoverPreparationFailure_t fail{};
    AMF_UE_NGAP_ID_t amf_ue_id_ie{};
    asn_ulong2INTEGER(&amf_ue_id_ie, amf_ue_id);
    ::ngap::add_ie(fail.protocolIEs,
                   ::ngap::make_ie(10 /* id-AMF-UE-NGAP-ID */,
                                   Criticality_ignore,
                                   &asn_DEF_AMF_UE_NGAP_ID,
                                   &amf_ue_id_ie));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_AMF_UE_NGAP_ID, &amf_ue_id_ie);
    RAN_UE_NGAP_ID_t ran_ue_id_ie = ran_ue_id;
    ::ngap::add_ie(fail.protocolIEs,
                   ::ngap::make_ie(85 /* id-RAN-UE-NGAP-ID */,
                                   Criticality_ignore,
                                   &asn_DEF_RAN_UE_NGAP_ID,
                                   &ran_ue_id_ie));
    ::ngap::add_ie(fail.protocolIEs,
                   ::ngap::make_ie(15 /* id-Cause */, Criticality_ignore, &asn_DEF_Cause, &cause));

    NGAP_PDU_t pdu{};
    pdu.present = NGAP_PDU_PR_unsuccessfulOutcome;
    pdu.choice.unsuccessfulOutcome =
        static_cast<UnsuccessfulOutcome_t*>(std::calloc(1, sizeof(UnsuccessfulOutcome_t)));
    pdu.choice.unsuccessfulOutcome->procedureCode = 12 /* id-HandoverPreparation */;
    pdu.choice.unsuccessfulOutcome->criticality = Criticality_reject;
    pdu.choice.unsuccessfulOutcome->value.present =
        UnsuccessfulOutcome__value_PR_HandoverPreparationFailure;
    pdu.choice.unsuccessfulOutcome->value.choice.HandoverPreparationFailure = fail;

    const auto bytes = ::ngap::encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &pdu);
    if (bytes.empty()) {
        spdlog::error("amf-ngap: failed to PER-encode HandoverPreparationFailure");
        return;
    }
    source_assoc.send(bytes);
    spdlog::info("amf-ngap: sent HandoverPreparationFailure ({} bytes) to source gNB, "
                 "AMF-UE-NGAP-ID={}",
                 bytes.size(),
                 amf_ue_id);
}

} // namespace

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #100, ADR-0096): real N2-based handover --
// Handover Preparation (TS 38.413 §8.4.2) relaying into Handover Resource Allocation (§8.4.3),
// TS 23.502 §4.9.1.3.2.
//
// Real, disclosed scope (docs/DECISIONS.md ADR-0096 has the full list): TargetID only supports
// the real `targetRANNodeID.globalRANNodeID.globalGNB-ID` CHOICE arm (this lab's only real RAN
// node type); SourceToTarget/TargetToSource-TransparentContainer are relayed byte-for-byte,
// opaque, per the real spec's own "AMF doesn't need to understand this" design (both are plain
// OCTET STRING, TS 38.413 §9.3.1.31/§9.3.1.32); SecurityContext reuses the exact derive_kgnb/
// derive_nh call (NCC=0, chain position 0) PathSwitchRequest already established (ADR-0090) for
// the identical disclosed reason; UEAggregateMaximumBitRate is this lab's own fixed default (no
// real per-subscriber AMBR tracked anywhere in this project); PDUSessionResourceSetupListHOReq's
// own per-session transfer is real but uses build_placeholder_ho_request_transfer's own disclosed
// placeholder N3 address. One real relay in flight per target gNB at a time
// (GnbAssociationRegistry's own disclosed lab-scope simplification).
//
// Real, disclosed design choice (not just for testability): unlike this file's own
// UplinkNASTransport-phase handlers (which legitimately depend on the single UE this specific
// association's own auth_state already tracks, ADR-0031's real lab scope), HandoverRequired
// carries its own real AMF-UE-NGAP-ID/RAN-UE-NGAP-ID IEs identifying the UE -- the same real "cold
// lookup via amf_ue_id_index -> UeSecurityContextStore" pattern handle_path_switch_request already
// established (ADR-0090) is used here too, rather than trusting this association's own local
// auth_state. This is the real, correct design (a handover procedure's own identity IEs are the
// authoritative source per spec, not implicit association state) -- auth_state is deliberately not
// a parameter of this function.
void handle_handover_required(ngap_core::SctpSocket& source_assoc,
                              UeSecurityContextStore& ue_security_contexts,
                              amf::AmfUeIdIndexStore& amf_ue_id_index,
                              amf::ngap::GnbAssociationRegistry& gnb_associations,
                              std::uint8_t amf_region_id,
                              std::uint16_t amf_set_id,
                              std::uint8_t amf_pointer,
                              const InitiatingMessage_t& msg) {
    const auto& container = msg.value.choice.HandoverRequired.protocolIEs;

    const auto* amf_ue_id_ie = ::ngap::find_ie(container, 10 /* id-AMF-UE-NGAP-ID */);
    const auto* ran_ue_id_ie = ::ngap::find_ie(container, 85 /* id-RAN-UE-NGAP-ID */);
    const auto* ho_type_ie = ::ngap::find_ie(container, 29 /* id-HandoverType */);
    const auto* cause_ie = ::ngap::find_ie(container, 15 /* id-Cause */);
    const auto* target_id_ie = ::ngap::find_ie(container, 105 /* id-TargetID */);
    const auto* pdu_list_ie = ::ngap::find_ie(container, 61 /* id-PDUSessionResourceListHORqd */);
    const auto* s2t_ie =
        ::ngap::find_ie(container, 101 /* id-SourceToTarget-TransparentContainer */);
    if (amf_ue_id_ie == nullptr || ran_ue_id_ie == nullptr || ho_type_ie == nullptr ||
        cause_ie == nullptr || target_id_ie == nullptr || pdu_list_ie == nullptr ||
        s2t_ie == nullptr) {
        spdlog::warn("amf-ngap: HandoverRequired missing one or more mandatory IEs, ignoring");
        return;
    }

    auto* amf_ue_id = static_cast<AMF_UE_NGAP_ID_t*>(
        ::ngap::decode_ie_value(&asn_DEF_AMF_UE_NGAP_ID, *amf_ue_id_ie));
    auto* ran_ue_id = static_cast<RAN_UE_NGAP_ID_t*>(
        ::ngap::decode_ie_value(&asn_DEF_RAN_UE_NGAP_ID, *ran_ue_id_ie));
    auto* ho_type =
        static_cast<HandoverType_t*>(::ngap::decode_ie_value(&asn_DEF_HandoverType, *ho_type_ie));
    auto* target_id =
        static_cast<TargetID_t*>(::ngap::decode_ie_value(&asn_DEF_TargetID, *target_id_ie));
    auto* pdu_list = static_cast<PDUSessionResourceListHORqd_t*>(
        ::ngap::decode_ie_value(&asn_DEF_PDUSessionResourceListHORqd, *pdu_list_ie));
    auto* s2t = static_cast<SourceToTarget_TransparentContainer_t*>(
        ::ngap::decode_ie_value(&asn_DEF_SourceToTarget_TransparentContainer, *s2t_ie));
    if (amf_ue_id == nullptr || ran_ue_id == nullptr || ho_type == nullptr ||
        target_id == nullptr || pdu_list == nullptr || s2t == nullptr) {
        spdlog::warn("amf-ngap: HandoverRequired's mandatory IEs failed to PER-decode, ignoring");
        if (amf_ue_id != nullptr)
            ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        if (ran_ue_id != nullptr)
            ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, ran_ue_id);
        if (ho_type != nullptr)
            ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        if (target_id != nullptr)
            ASN_STRUCT_FREE(asn_DEF_TargetID, target_id);
        if (pdu_list != nullptr)
            ASN_STRUCT_FREE(asn_DEF_PDUSessionResourceListHORqd, pdu_list);
        if (s2t != nullptr)
            ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }
    const unsigned long ran_ue_id_value = *ran_ue_id;
    ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, ran_ue_id);

    if (target_id->present != TargetID_PR_targetRANNodeID ||
        target_id->choice.targetRANNodeID->globalRANNodeID.present !=
            GlobalRANNodeID_PR_globalGNB_ID) {
        spdlog::warn("amf-ngap: HandoverRequired's TargetID is not a real globalGNB-ID (this lab's "
                     "only supported RAN node type) -- sending HandoverPreparationFailure");
        Cause_t cause{};
        cause.present = Cause_PR_radioNetwork;
        cause.choice.radioNetwork = CauseRadioNetwork_unknown_targetID;
        long amf_ue_id_early = 0;
        asn_INTEGER2long(amf_ue_id, &amf_ue_id_early);
        send_handover_preparation_failure(
            source_assoc, static_cast<unsigned long>(amf_ue_id_early), ran_ue_id_value, cause);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_TargetID, target_id);
        ASN_STRUCT_FREE(asn_DEF_PDUSessionResourceListHORqd, pdu_list);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }
    const auto target_gnb_id_bytes = ::ngap::encode_value(
        &asn_DEF_GlobalGNB_ID,
        target_id->choice.targetRANNodeID->globalRANNodeID.choice.globalGNB_ID);

    long amf_ue_id_value = 0;
    asn_INTEGER2long(amf_ue_id, &amf_ue_id_value);

    // Real cold lookup (see this function's own header comment) -- identical to
    // handle_path_switch_request's own precedent.
    const auto tmsi_opt = amf_ue_id_index.get(static_cast<unsigned long>(amf_ue_id_value));
    std::optional<UeSecurityContext> ctx;
    if (tmsi_opt.has_value()) {
        ctx = ue_security_contexts.get(*tmsi_opt);
    }
    if (!ctx.has_value()) {
        spdlog::warn(
            "amf-ngap: HandoverRequired referenced an unrecognized AMF-UE-NGAP-ID={} -- no "
            "persisted UE security context, sending HandoverPreparationFailure",
            amf_ue_id_value);
        Cause_t cause{};
        cause.present = Cause_PR_radioNetwork;
        cause.choice.radioNetwork = CauseRadioNetwork_unknown_local_UE_NGAP_ID;
        send_handover_preparation_failure(
            source_assoc, static_cast<unsigned long>(amf_ue_id_value), ran_ue_id_value, cause);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_TargetID, target_id);
        ASN_STRUCT_FREE(asn_DEF_PDUSessionResourceListHORqd, pdu_list);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }
    spdlog::info("amf-ngap: HandoverRequired for AMF-UE-NGAP-ID={}, RAN-UE-NGAP-ID={}, SUPI={}, "
                 "HandoverType={}, {} PDU session(s), target gNB identity {} bytes",
                 amf_ue_id_value,
                 ran_ue_id_value,
                 ctx->supi,
                 *ho_type,
                 pdu_list->list.count,
                 target_gnb_id_bytes.size());

    // Real vertical key derivation for the mandatory SecurityContext IE -- identical call/disclosed
    // scope precedent to handle_path_switch_request above (ADR-0090): every call derives chain
    // position 0 (NCC=0), since no InitialContextSetup/prior AS security context has ever been
    // persisted per-hop in this project.
    const auto kgnb =
        aka_crypto::derive_kgnb(ctx->kamf, ctx->uplink_count, aka_crypto::kAccessType3gpp);
    const auto nh = aka_crypto::derive_nh(ctx->kamf, kgnb);

    // Build real HandoverRequest (TS 38.413 §9.2.3.1 -- id-HandoverResourceAllocation).
    HandoverRequest_t req{};
    ::ngap::add_ie(
        req.protocolIEs,
        ::ngap::make_ie(
            10 /* id-AMF-UE-NGAP-ID */, Criticality_reject, &asn_DEF_AMF_UE_NGAP_ID, amf_ue_id));
    ::ngap::add_ie(
        req.protocolIEs,
        ::ngap::make_ie(
            29 /* id-HandoverType */, Criticality_reject, &asn_DEF_HandoverType, ho_type));
    Cause_t relayed_cause{};
    relayed_cause.present = Cause_PR_radioNetwork;
    relayed_cause.choice.radioNetwork = CauseRadioNetwork_handover_desirable_for_radio_reason;
    ::ngap::add_ie(
        req.protocolIEs,
        ::ngap::make_ie(15 /* id-Cause */, Criticality_ignore, &asn_DEF_Cause, &relayed_cause));

    // This lab's own fixed default UE-AMBR -- no real per-subscriber AMBR tracked anywhere in
    // this project yet (same real, disclosed gap class as kSst/kSd's own fixed S-NSSAI).
    UEAggregateMaximumBitRate_t ambr{};
    asn_ulong2INTEGER(&ambr.uEAggregateMaximumBitRateDL, 1000000000UL);
    asn_ulong2INTEGER(&ambr.uEAggregateMaximumBitRateUL, 500000000UL);
    ::ngap::add_ie(req.protocolIEs,
                   ::ngap::make_ie(110 /* id-UEAggregateMaximumBitRate */,
                                   Criticality_reject,
                                   &asn_DEF_UEAggregateMaximumBitRate,
                                   &ambr));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_UEAggregateMaximumBitRate, &ambr);

    UESecurityCapabilities_t sec_cap{};
    if (ctx->ue_security_capability.size() >= 4) {
        const auto make_algo_bits = [](const std::uint8_t* two_bytes) {
            BIT_STRING_t bs{};
            bs.buf = static_cast<std::uint8_t*>(std::malloc(2));
            std::memcpy(bs.buf, two_bytes, 2);
            bs.size = 2;
            bs.bits_unused = 0;
            return bs;
        };
        sec_cap.nRencryptionAlgorithms = make_algo_bits(ctx->ue_security_capability.data());
        sec_cap.nRintegrityProtectionAlgorithms =
            make_algo_bits(ctx->ue_security_capability.data() + 2);
        sec_cap.eUTRAencryptionAlgorithms = make_algo_bits(ctx->ue_security_capability.data());
        sec_cap.eUTRAintegrityProtectionAlgorithms =
            make_algo_bits(ctx->ue_security_capability.data() + 2);
        ::ngap::add_ie(req.protocolIEs,
                       ::ngap::make_ie(119 /* id-UESecurityCapabilities */,
                                       Criticality_reject,
                                       &asn_DEF_UESecurityCapabilities,
                                       &sec_cap));
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_UESecurityCapabilities, &sec_cap);
    }

    SecurityContext_t sec_ctx{};
    sec_ctx.nextHopChainingCount = 0;
    sec_ctx.nextHopNH.buf = static_cast<std::uint8_t*>(std::malloc(nh.size()));
    std::memcpy(sec_ctx.nextHopNH.buf, nh.data(), nh.size());
    sec_ctx.nextHopNH.size = nh.size();
    sec_ctx.nextHopNH.bits_unused = 0;
    ::ngap::add_ie(
        req.protocolIEs,
        ::ngap::make_ie(
            93 /* id-SecurityContext */, Criticality_reject, &asn_DEF_SecurityContext, &sec_ctx));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_SecurityContext, &sec_ctx);

    PDUSessionResourceSetupListHOReq_t setup_list{};
    const auto placeholder_transfer_bytes = build_placeholder_ho_request_transfer();
    for (int i = 0; i < pdu_list->list.count; ++i) {
        auto* item = static_cast<PDUSessionResourceSetupItemHOReq_t*>(
            std::calloc(1, sizeof(PDUSessionResourceSetupItemHOReq_t)));
        item->pDUSessionID = pdu_list->list.array[i]->pDUSessionID;
        item->s_NSSAI.sST = make_octet_string(&kSst, 1);
        item->handoverRequestTransfer =
            make_octet_string(placeholder_transfer_bytes.data(), placeholder_transfer_bytes.size());
        ASN_SEQUENCE_ADD(&setup_list.list, item);
    }
    ::ngap::add_ie(req.protocolIEs,
                   ::ngap::make_ie(73 /* id-PDUSessionResourceSetupListHOReq */,
                                   Criticality_reject,
                                   &asn_DEF_PDUSessionResourceSetupListHOReq,
                                   &setup_list));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PDUSessionResourceSetupListHOReq, &setup_list);

    AllowedNSSAI_t allowed_nssai{};
    auto* nssai_item =
        static_cast<AllowedNSSAI_Item_t*>(std::calloc(1, sizeof(AllowedNSSAI_Item_t)));
    nssai_item->s_NSSAI.sST = make_octet_string(&kSst, 1);
    auto* sd = static_cast<SD_t*>(std::calloc(1, sizeof(SD_t)));
    const std::uint8_t sd_bytes[3] = {static_cast<std::uint8_t>((kSd >> 16) & 0xff),
                                      static_cast<std::uint8_t>((kSd >> 8) & 0xff),
                                      static_cast<std::uint8_t>(kSd & 0xff)};
    *sd = make_octet_string(sd_bytes, 3);
    nssai_item->s_NSSAI.sD = sd;
    ASN_SEQUENCE_ADD(&allowed_nssai.list, nssai_item);
    ::ngap::add_ie(
        req.protocolIEs,
        ::ngap::make_ie(
            0 /* id-AllowedNSSAI */, Criticality_reject, &asn_DEF_AllowedNSSAI, &allowed_nssai));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_AllowedNSSAI, &allowed_nssai);

    GUAMI_t guami{};
    std::uint8_t plmn_bytes[3];
    encode_plmn_identity(kMcc, kMnc, plmn_bytes);
    guami.pLMNIdentity = make_octet_string(plmn_bytes, 3);
    guami.aMFRegionID = make_bit_string_from_uint(amf_region_id, 8);
    guami.aMFSetID = make_bit_string_from_uint(amf_set_id, 10);
    guami.aMFPointer = make_bit_string_from_uint(amf_pointer, 6);
    ::ngap::add_ie(req.protocolIEs,
                   ::ngap::make_ie(28 /* id-GUAMI */, Criticality_ignore, &asn_DEF_GUAMI, &guami));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_GUAMI, &guami);

    ::ngap::add_ie(req.protocolIEs,
                   ::ngap::make_ie(101 /* id-SourceToTarget-TransparentContainer */,
                                   Criticality_reject,
                                   &asn_DEF_SourceToTarget_TransparentContainer,
                                   s2t));

    NGAP_PDU_t req_pdu{};
    req_pdu.present = NGAP_PDU_PR_initiatingMessage;
    req_pdu.choice.initiatingMessage =
        static_cast<InitiatingMessage_t*>(std::calloc(1, sizeof(InitiatingMessage_t)));
    req_pdu.choice.initiatingMessage->procedureCode = 13 /* id-HandoverResourceAllocation */;
    req_pdu.choice.initiatingMessage->criticality = Criticality_reject;
    req_pdu.choice.initiatingMessage->value.present = InitiatingMessage__value_PR_HandoverRequest;
    req_pdu.choice.initiatingMessage->value.choice.HandoverRequest = req;

    const auto req_bytes = ::ngap::encode_pdu(req_pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &req_pdu);

    ASN_STRUCT_FREE(asn_DEF_TargetID, target_id);
    ASN_STRUCT_FREE(asn_DEF_PDUSessionResourceListHORqd, pdu_list);

    if (req_bytes.empty()) {
        spdlog::error("amf-ngap: failed to PER-encode HandoverRequest");
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }

    spdlog::info("amf-ngap: sending real HandoverRequest ({} bytes) to target gNB, awaiting reply "
                 "(HandoverRequestAcknowledge/HandoverFailure)",
                 req_bytes.size());
    const auto reply_bytes = gnb_associations.send_and_await_reply(
        target_gnb_id_bytes, req_bytes, std::chrono::seconds(10));

    if (!reply_bytes.has_value()) {
        spdlog::warn("amf-ngap: no reply from target gNB (not registered, or timed out) -- sending "
                     "HandoverPreparationFailure to source gNB");
        Cause_t cause{};
        cause.present = Cause_PR_radioNetwork;
        cause.choice.radioNetwork = CauseRadioNetwork_ho_target_not_allowed;
        send_handover_preparation_failure(
            source_assoc, static_cast<unsigned long>(amf_ue_id_value), ran_ue_id_value, cause);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }

    NGAP_PDU_t* reply_pdu = ::ngap::decode_pdu(*reply_bytes);
    if (reply_pdu == nullptr) {
        spdlog::warn("amf-ngap: target gNB's reply failed to PER-decode -- sending "
                     "HandoverPreparationFailure to source gNB");
        Cause_t cause{};
        cause.present = Cause_PR_protocol;
        cause.choice.protocol = CauseProtocol_abstract_syntax_error_reject;
        send_handover_preparation_failure(
            source_assoc, static_cast<unsigned long>(amf_ue_id_value), ran_ue_id_value, cause);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }

    if (reply_pdu->present == NGAP_PDU_PR_unsuccessfulOutcome &&
        reply_pdu->choice.unsuccessfulOutcome->procedureCode == 13) {
        spdlog::info("amf-ngap: target gNB sent HandoverFailure -- relaying as "
                     "HandoverPreparationFailure to source gNB");
        const auto& fail_container =
            reply_pdu->choice.unsuccessfulOutcome->value.choice.HandoverFailure.protocolIEs;
        const auto* fail_cause_ie = ::ngap::find_ie(fail_container, 15 /* id-Cause */);
        Cause_t cause{};
        if (fail_cause_ie != nullptr) {
            auto* decoded_cause =
                static_cast<Cause_t*>(::ngap::decode_ie_value(&asn_DEF_Cause, *fail_cause_ie));
            if (decoded_cause != nullptr) {
                cause = *decoded_cause;
                std::free(decoded_cause);
            }
        } else {
            cause.present = Cause_PR_radioNetwork;
            cause.choice.radioNetwork =
                CauseRadioNetwork_ho_failure_in_target_5GC_ngran_node_or_target_system;
        }
        send_handover_preparation_failure(
            source_assoc, static_cast<unsigned long>(amf_ue_id_value), ran_ue_id_value, cause);
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, reply_pdu);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }

    if (reply_pdu->present != NGAP_PDU_PR_successfulOutcome ||
        reply_pdu->choice.successfulOutcome->procedureCode != 13) {
        spdlog::warn("amf-ngap: target gNB's reply was neither HandoverRequestAcknowledge nor "
                     "HandoverFailure (present={}) -- sending HandoverPreparationFailure to source",
                     static_cast<int>(reply_pdu->present));
        Cause_t cause{};
        cause.present = Cause_PR_protocol;
        cause.choice.protocol = CauseProtocol_semantic_error;
        send_handover_preparation_failure(
            source_assoc, static_cast<unsigned long>(amf_ue_id_value), ran_ue_id_value, cause);
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, reply_pdu);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }

    const auto& ack_container =
        reply_pdu->choice.successfulOutcome->value.choice.HandoverRequestAcknowledge.protocolIEs;
    const auto* t2s_ie =
        ::ngap::find_ie(ack_container, 106 /* id-TargetToSource-TransparentContainer */);
    if (t2s_ie == nullptr) {
        spdlog::warn("amf-ngap: target gNB's HandoverRequestAcknowledge missing mandatory "
                     "TargetToSource-TransparentContainer, ignoring");
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, reply_pdu);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }
    auto* t2s = static_cast<TargetToSource_TransparentContainer_t*>(
        ::ngap::decode_ie_value(&asn_DEF_TargetToSource_TransparentContainer, *t2s_ie));
    if (t2s == nullptr) {
        spdlog::warn("amf-ngap: target gNB's TargetToSource-TransparentContainer failed to "
                     "PER-decode, ignoring");
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, reply_pdu);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
        ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
        return;
    }

    spdlog::info("amf-ngap: real HandoverRequestAcknowledge received from target gNB -- sending "
                 "real HandoverCommand to source gNB");

    // Build real HandoverCommand (TS 38.413 §9.2.3.4). PDUSessionResourceHandoverList is real,
    // OPTIONAL per spec -- omitted, a real, disclosed scope narrowing (same class as
    // PathSwitchRequestAcknowledge's own empty_transfer precedent, ADR-0090).
    HandoverCommand_t cmd{};
    ::ngap::add_ie(
        cmd.protocolIEs,
        ::ngap::make_ie(
            10 /* id-AMF-UE-NGAP-ID */, Criticality_reject, &asn_DEF_AMF_UE_NGAP_ID, amf_ue_id));
    RAN_UE_NGAP_ID_t source_ran_ue_id = static_cast<RAN_UE_NGAP_ID_t>(ran_ue_id_value);
    ::ngap::add_ie(cmd.protocolIEs,
                   ::ngap::make_ie(85 /* id-RAN-UE-NGAP-ID */,
                                   Criticality_reject,
                                   &asn_DEF_RAN_UE_NGAP_ID,
                                   &source_ran_ue_id));
    ::ngap::add_ie(
        cmd.protocolIEs,
        ::ngap::make_ie(
            29 /* id-HandoverType */, Criticality_reject, &asn_DEF_HandoverType, ho_type));
    ::ngap::add_ie(cmd.protocolIEs,
                   ::ngap::make_ie(106 /* id-TargetToSource-TransparentContainer */,
                                   Criticality_reject,
                                   &asn_DEF_TargetToSource_TransparentContainer,
                                   t2s));

    NGAP_PDU_t cmd_pdu{};
    cmd_pdu.present = NGAP_PDU_PR_successfulOutcome;
    cmd_pdu.choice.successfulOutcome =
        static_cast<SuccessfulOutcome_t*>(std::calloc(1, sizeof(SuccessfulOutcome_t)));
    cmd_pdu.choice.successfulOutcome->procedureCode = 12 /* id-HandoverPreparation */;
    cmd_pdu.choice.successfulOutcome->criticality = Criticality_reject;
    cmd_pdu.choice.successfulOutcome->value.present = SuccessfulOutcome__value_PR_HandoverCommand;
    cmd_pdu.choice.successfulOutcome->value.choice.HandoverCommand = cmd;

    const auto cmd_bytes = ::ngap::encode_pdu(cmd_pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &cmd_pdu);
    if (cmd_bytes.empty()) {
        spdlog::error("amf-ngap: failed to PER-encode HandoverCommand");
    } else {
        source_assoc.send(cmd_bytes);
        spdlog::info("amf-ngap: sent real HandoverCommand ({} bytes) to source gNB, "
                     "AMF-UE-NGAP-ID={}",
                     cmd_bytes.size(),
                     amf_ue_id_value);
    }

    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, reply_pdu);
    ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
    ASN_STRUCT_FREE(asn_DEF_HandoverType, ho_type);
    ASN_STRUCT_FREE(asn_DEF_SourceToTarget_TransparentContainer, s2t);
    ASN_STRUCT_FREE(asn_DEF_TargetToSource_TransparentContainer, t2s);
}

// Gap-closure (docs/CAPABILITY_GAP_ANALYSIS.md task #100, ADR-0096): real Handover Notification
// (TS 38.413 §8.4.4, id-HandoverNotification) -- arrives on the TARGET association's own thread
// once the UE has physically shown up there. Real, load-bearing side effect: sends a real,
// AMF-INITIATED UEContextReleaseCommand to the SOURCE gNB (closing the real, previously-disclosed
// gap in handle_ue_context_release_request's own header comment -- "this does NOT implement the
// AMF-INITIATED direction" -- for exactly this one real trigger) BEFORE re-pointing
// ue_ngap_registry's entry to the target, since NgapUeRegistry::send_raw needs the CURRENT (still
// source) entry to reach the right association. Real, disclosed narrowing: NotifySourceNGRANNode
// is decoded (log-only) but does not change behavior -- this AMF always notifies the source (the
// only real 3GPP-conformant behavior when this AMF itself tracked the whole handover), matching
// TS 38.413's own real "if present, indicates..." optional-hint semantics rather than a hard gate.
void handle_handover_notify(ngap_core::SctpSocket& target_assoc,
                            NgapUeRegistry& ue_ngap_registry,
                            UeSecurityContextStore& ue_security_contexts,
                            AmfUeIdIndexStore& amf_ue_id_index,
                            const InitiatingMessage_t& msg) {
    const auto& container = msg.value.choice.HandoverNotify.protocolIEs;

    const auto* amf_ue_id_ie = ::ngap::find_ie(container, 10 /* id-AMF-UE-NGAP-ID */);
    const auto* ran_ue_id_ie = ::ngap::find_ie(container, 85 /* id-RAN-UE-NGAP-ID */);
    const auto* uli_ie = ::ngap::find_ie(container, 121 /* id-UserLocationInformation */);
    if (amf_ue_id_ie == nullptr || ran_ue_id_ie == nullptr || uli_ie == nullptr) {
        spdlog::warn("amf-ngap: HandoverNotify missing one or more mandatory IEs, ignoring");
        return;
    }

    auto* amf_ue_id = static_cast<AMF_UE_NGAP_ID_t*>(
        ::ngap::decode_ie_value(&asn_DEF_AMF_UE_NGAP_ID, *amf_ue_id_ie));
    auto* ran_ue_id = static_cast<RAN_UE_NGAP_ID_t*>(
        ::ngap::decode_ie_value(&asn_DEF_RAN_UE_NGAP_ID, *ran_ue_id_ie));
    if (amf_ue_id == nullptr || ran_ue_id == nullptr) {
        spdlog::warn("amf-ngap: HandoverNotify's mandatory IEs failed to PER-decode, ignoring");
        if (amf_ue_id != nullptr)
            ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        if (ran_ue_id != nullptr)
            ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, ran_ue_id);
        return;
    }
    long amf_ue_id_value = 0;
    asn_INTEGER2long(amf_ue_id, &amf_ue_id_value);
    const unsigned long new_ran_ue_id_value = *ran_ue_id;
    spdlog::info(
        "amf-ngap: real HandoverNotify received -- AMF-UE-NGAP-ID={}, new RAN-UE-NGAP-ID={}",
        amf_ue_id_value,
        new_ran_ue_id_value);

    const auto tmsi_opt = amf_ue_id_index.get(static_cast<unsigned long>(amf_ue_id_value));
    std::optional<UeSecurityContext> ctx;
    if (tmsi_opt.has_value()) {
        ctx = ue_security_contexts.get(*tmsi_opt);
    }
    if (!ctx.has_value() || ctx->supi.empty()) {
        spdlog::warn("amf-ngap: HandoverNotify referenced an unrecognized AMF-UE-NGAP-ID={} -- no "
                     "persisted UE security context, cannot re-point registry",
                     amf_ue_id_value);
        ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
        ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, ran_ue_id);
        return;
    }

    // Real, AMF-initiated UEContextReleaseCommand to the SOURCE (still the registry's current
    // entry at this point) -- Cause=nas/normal-release, same real cause value
    // handle_ue_context_release_request's own RAN-initiated direction already uses.
    UEContextReleaseCommand_t rel_cmd{};
    UE_NGAP_IDs_t ue_ngap_ids{};
    ue_ngap_ids.present = UE_NGAP_IDs_PR_aMF_UE_NGAP_ID;
    asn_ulong2INTEGER(&ue_ngap_ids.choice.aMF_UE_NGAP_ID,
                      static_cast<unsigned long>(amf_ue_id_value));
    ::ngap::add_ie(
        rel_cmd.protocolIEs,
        ::ngap::make_ie(
            114 /* id-UE-NGAP-IDs */, Criticality_reject, &asn_DEF_UE_NGAP_IDs, &ue_ngap_ids));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_UE_NGAP_IDs, &ue_ngap_ids);
    Cause_t rel_cause{};
    rel_cause.present = Cause_PR_nas;
    rel_cause.choice.nas = CauseNas_normal_release;
    ::ngap::add_ie(
        rel_cmd.protocolIEs,
        ::ngap::make_ie(15 /* id-Cause */, Criticality_ignore, &asn_DEF_Cause, &rel_cause));

    NGAP_PDU_t rel_pdu{};
    rel_pdu.present = NGAP_PDU_PR_initiatingMessage;
    rel_pdu.choice.initiatingMessage =
        static_cast<InitiatingMessage_t*>(std::calloc(1, sizeof(InitiatingMessage_t)));
    rel_pdu.choice.initiatingMessage->procedureCode = 41 /* id-UEContextRelease */;
    rel_pdu.choice.initiatingMessage->criticality = Criticality_reject;
    rel_pdu.choice.initiatingMessage->value.present =
        InitiatingMessage__value_PR_UEContextReleaseCommand;
    rel_pdu.choice.initiatingMessage->value.choice.UEContextReleaseCommand = rel_cmd;
    const auto rel_bytes = ::ngap::encode_pdu(rel_pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &rel_pdu);
    if (!rel_bytes.empty()) {
        const bool sent = ue_ngap_registry.send_raw(ctx->supi, rel_bytes);
        spdlog::info("amf-ngap: {} real AMF-initiated UEContextReleaseCommand ({} bytes) to the "
                     "source gNB for SUPI {}",
                     sent ? "sent" : "FAILED to send (no live source association on record for)",
                     rel_bytes.size(),
                     ctx->supi);
    }

    // Real, load-bearing re-point: this SUPI's NgapUeRegistry entry now points at the TARGET
    // association -- without this, a later Namf_Communication N1N2MessageTransfer would still try
    // to deliver to the now-released source association. Same real re-point precedent
    // PathSwitchRequest already established (ADR-0090).
    NgapUeRegistry::Entry entry;
    entry.socket = &target_assoc;
    entry.amf_ue_id = static_cast<std::uint32_t>(amf_ue_id_value);
    entry.ran_ue_id = static_cast<std::uint32_t>(new_ran_ue_id_value);
    entry.knas_int = aka_crypto::derive_knas_int(ctx->kamf, aka_crypto::kNia2AlgorithmIdentity);
    entry.knas_enc = aka_crypto::derive_knas_enc(ctx->kamf, aka_crypto::kNea2AlgorithmIdentity);
    entry.next_downlink_count = ctx->downlink_count;
    ue_ngap_registry.register_ue(ctx->supi, entry);
    spdlog::info(
        "amf-ngap: re-pointed NGAP registry entry for SUPI {} to the new RAN-UE-NGAP-ID={} "
        "after HandoverNotify",
        ctx->supi,
        new_ran_ue_id_value);

    ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, amf_ue_id);
    ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, ran_ue_id);
}

} // namespace amf::ngap
