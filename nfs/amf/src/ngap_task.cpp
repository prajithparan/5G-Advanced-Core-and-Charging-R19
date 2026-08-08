#include "ngap_task.hpp"

#include "nas_codec.hpp"
#include "ngap_codec.hpp"

#include "aka_crypto/hex.hpp"
#include "aka_crypto/kdf.hpp"

#include "ngap_core/sctp_socket.hpp"

#include "sbi_core/http2_client.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/multipart.hpp"
#include "sbi_core/oauth2_client.hpp"
#include "sbi_core/tls_config.hpp"

#include "TS29122_CommonData_grp.hpp"
#include "TS29509_Nausf_UEAuthentication.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>

extern "C" {
#include <AMF-UE-NGAP-ID.h>
#include <AMFName.h>
#include <AMFPointer.h>
#include <AMFRegionID.h>
#include <AMFSetID.h>
#include <DownlinkNASTransport.h>
#include <GUAMI.h>
#include <InitialUEMessage.h>
#include <InitiatingMessage.h>
#include <NAS-PDU.h>
#include <NGAP-PDU.h>
#include <NGSetupRequest.h>
#include <NGSetupResponse.h>
#include <PLMNIdentity.h>
#include <PLMNSupportItem.h>
#include <PLMNSupportList.h>
#include <ProcedureCode.h>
#include <RAN-UE-NGAP-ID.h>
#include <RelativeAMFCapacity.h>
#include <SD.h>
#include <SST.h>
#include <S-NSSAI.h>
#include <ServedGUAMIItem.h>
#include <ServedGUAMIList.h>
#include <SliceSupportItem.h>
#include <SliceSupportList.h>
#include <SuccessfulOutcome.h>
#include <UplinkNASTransport.h>
}

namespace amf::ngap {

namespace {

// This lab's fixed test PLMN/AMF identity, matching simulators/ransim/config/gnb.yaml's
// mcc=999/mnc=70/tac=1/sst=1/sd=1 (see docs/DECISIONS.md ADR-0016) -- not invented, the same
// values already used throughout this project's certs/UDM/PCF test configuration. AMF Region ID/
// Set ID/Pointer (TS 23.003 clause 2.10.1's 8+10+6-bit AMF Identifier split) are set to all-zero
// as an arbitrary, unambiguous lab placeholder -- disclosed, not sourced from any real deployment
// plan, chosen specifically to sidestep bit-packing direction ambiguity for this first stage
// (an all-zero bit pattern is unambiguous regardless of MSB/LSB-first packing convention).
constexpr const char* kMcc = "999";
constexpr const char* kMnc = "70";
constexpr std::uint8_t kSst = 1;
constexpr std::uint32_t kSd = 1;
constexpr const char* kAmfName = "5gc-r19-amf";

// AUSF's own address/API root (nfs/ausf/src/main.cpp's kPort=7782, kApiRoot="/nausf-auth/v1") --
// not shared via a common header, matching this file's existing pattern of locally duplicating
// the other NFs' fixed lab constants (kMcc/kMnc above) rather than reaching into another NF's
// private headers (CLAUDE.md's "no NF includes another NF's private headers" rule).
constexpr const char* kAusfBase = "https://127.0.0.1:7782";

// PCF's own address/API root (nfs/pcf/src/main.cpp's kPort=7783), and this AMF's own address --
// same locally-duplicated-constant convention as kAusfBase above. kSelfBase feeds
// PolicyAssociationRequest.notificationUri, a mandatory field PCF's schema requires even though
// nothing here implements a receiver for it yet -- same disclosed, deliberately-deferred gap
// nfs/pcf/src/main.cpp's own file header already states for both directions of this callback.
constexpr const char* kPcfBase = "https://127.0.0.1:7783";
constexpr const char* kSelfBase = "https://127.0.0.1:7778";

// SMF's own address (nfs/smf/src/main.cpp's kPort=7779) -- same locally-duplicated-constant
// convention as kAusfBase/kPcfBase above.
constexpr const char* kSmfBase = "https://127.0.0.1:7779";

// TS 23.003 §28.3.2.5 serving network name format ("5G:mnc<3-digit>.mcc<3-digit>.
// 3gppnetwork.org"), zero-padded MNC even for this lab's 2-digit mnc=70 -- same string this
// project's own AUSF integration tests already use (tests/integration/test_ausf_ue_authentication.
// cpp), not invented here.
constexpr const char* kServingNetworkName = "5G:mnc070.mcc999.3gppnetwork.org";

// This lab's single-UE-at-a-time scope (see docs/DECISIONS.md ADR-0031) makes a simple
// monotonically increasing counter a correct, unambiguous AMF-UE-NGAP-ID allocator -- a real AMF
// serving many concurrent UEs would need a real allocation/reuse scheme.
std::atomic<unsigned long> g_next_amf_ue_ngap_id{1};

// Minimal per-association state carried from Stage 2's InitialUEMessage handler to Stage 3's
// UplinkNASTransport handler -- both are separate, otherwise-stateless per-message handlers, but
// Stage 3's AUSF confirmation call needs Stage 2's SUPI and the auth-context confirmation URL
// AUSF returned when it created the authentication context. This lab's single-UE-at-a-time scope
// (ADR-0031) makes one flat struct in handle_association's own stack frame correct and
// unambiguous -- a real AMF tracking many concurrent UEs would need this keyed by
// RAN-UE-NGAP-ID/AMF-UE-NGAP-ID in a real UE context store instead.
struct UeAuthState {
    std::string supi;
    // Full path AUSF returned in UEAuthenticationCtx._links["5g-aka"]["href"] (already includes
    // the auth context id) -- not reconstructed from parts, since AUSF owns that ID's format.
    std::string confirmation_path;
    // Raw UE Security Capability TLV value bytes captured from the RegistrationRequest (Stage 2),
    // replayed verbatim in Stage 4's SecurityModeCommand -- see
    // amf::nas::RegistrationRequestInfo::ue_security_capability's own comment for why.
    std::vector<std::uint8_t> ue_security_capability;
    // Allocated in Stage 2 (handle_initial_ue_message) and reused for every later
    // DownlinkNASTransport this association sends (Stage 4's SecurityModeCommand, and beyond) --
    // this lab's single-UE-at-a-time scope (ADR-0031) makes storing them here, once, correct.
    unsigned long amf_ue_id = 0;
    unsigned long ran_ue_id = 0;
    // Populated once Stage 4 derives them from KAMF (TS 33.501 Annex A.8) -- see
    // aka_crypto/kdf.hpp's derive_knas_int/derive_knas_enc.
    std::optional<aka_crypto::NasIntKey> knas_int;
    std::optional<aka_crypto::NasEncKey> knas_enc;
    // The RAND from the most recently sent AuthenticationRequest -- needed if THIS attempt itself
    // gets an AuthenticationFailure+AUTS back (SQN resync, ADR-0037): decoding AUTS only works
    // with the exact RAND the UE used, which the AuthenticationFailure message itself doesn't
    // repeat.
    std::optional<aka_crypto::Key128> last_auth_rand;
    // At most one resync retry per association -- this lab's scope (ADR-0031); a real AMF would
    // still cap retries (TS 33.102 doesn't allow unbounded resync loops either), just possibly
    // higher than 1.
    bool sqn_resync_attempted = false;
    // Which UplinkNASTransport this association is next expecting -- this lab's
    // single-registration-per-association scope (ADR-0031) makes a simple linear phase enum a
    // correct dispatch key; a real AMF would need a full per-UE 5GMM state machine.
    enum class Phase {
        AwaitingAuthenticationResponse,
        AwaitingSecurityModeComplete,
        // No AwaitingRegistrationComplete phase: TS 24.501's real UE behavior (confirmed via
        // source, simulators/ransim/vendor/UERANSIM/src/ue/nas/mm/register.cpp:346-426) only
        // sends RegistrationComplete if RegistrationAccept carried a 5G-GUTI, an NSSCI=CHANGED
        // indication, or a configuredNSSAI -- this build's encode_registration_accept sends none
        // of those (disclosed simplification, see its own comment), so a real UE never sends
        // RegistrationComplete in response. Confirmed via real interop: AMF proceeds straight to
        // AwaitingPduSessionEstablishmentRequest once RegistrationAccept is sent.
        AwaitingPduSessionEstablishmentRequest,
        Done,
    };
    Phase phase = Phase::AwaitingAuthenticationResponse;
};

// TS 33.501 Annex A.7's SUPI input for KAMF derivation is the bare identity digits, NOT the
// "imsi-"-prefixed SBI string representation this project otherwise uses for auth_state.supi
// everywhere else (AUSF/PCF/SMF calls all correctly use the full "imsi-..." string -- confirmed
// working, since those calls succeed). Confirmed via real interop, not assumed: a first attempt
// using the full "imsi-..." string produced a KAMF the UE could never converge on
// (SecurityModeCommand MAC verification failed against a real nr-ue), tracked down to
// simulators/ransim/vendor/UERANSIM/src/utils/common_types.cpp's own Supi::Parse stripping the
// "imsi-" prefix before its own KDF calls (e.g. keys.cpp:33's
// `EncodeKdfString(ueConfig.supi->value)` -- `value` is deliberately prefix-free). Only this
// project's own "imsi-" prefix convention needs stripping -- a real SUCI-derived, non-IMSI-format
// SUPI is out of scope (matches decode_registration_request's own existing IMSI-only scope). See
// docs/DECISIONS.md ADR-0037.
std::string strip_imsi_prefix(const std::string& supi) {
    static constexpr char kPrefix[] = "imsi-";
    static constexpr std::size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (supi.compare(0, kPrefixLen, kPrefix) == 0) {
        return supi.substr(kPrefixLen);
    }
    return supi;
}

// Shared by every Stage 2+/4 handler that needs to pull the NAS-PDU out of an UplinkNASTransport
// (previously duplicated inline in handle_uplink_nas_transport; factored out once a second caller
// -- the SecurityModeComplete handler -- needed the identical extraction).
std::optional<std::vector<std::uint8_t>> extract_uplink_nas_pdu(const InitiatingMessage_t& msg) {
    const auto& container = msg.value.choice.UplinkNASTransport.protocolIEs;
    const auto* nas_pdu_ie = ::ngap::find_ie(container, 38 /* id-NAS-PDU */);
    if (nas_pdu_ie == nullptr) {
        spdlog::warn("amf-ngap: UplinkNASTransport missing mandatory NAS-PDU IE, ignoring");
        return std::nullopt;
    }
    auto* nas_pdu = static_cast<NAS_PDU_t*>(::ngap::decode_ie_value(&asn_DEF_NAS_PDU, *nas_pdu_ie));
    if (nas_pdu == nullptr) {
        spdlog::warn("amf-ngap: UplinkNASTransport's NAS-PDU failed to PER-decode, ignoring");
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(nas_pdu->buf, nas_pdu->buf + nas_pdu->size);
    ASN_STRUCT_FREE(asn_DEF_NAS_PDU, nas_pdu);
    return bytes;
}

// PLMN identity encoding per TS 24.008/38.413 (half-octet BCD, filler 0xF for a 2-digit MNC's
// third digit) -- the same standard encoding every 3GPP PLMN identity field uses, not specific to
// NGAP.
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

OCTET_STRING_t make_octet_string(const std::uint8_t* data, std::size_t len) {
    OCTET_STRING_t s{};
    s.buf = static_cast<std::uint8_t*>(std::malloc(len));
    s.size = len;
    std::memcpy(s.buf, data, len);
    return s;
}

BIT_STRING_t make_zero_bit_string(std::size_t bits) {
    const std::size_t bytes = (bits + 7) / 8;
    BIT_STRING_t s{};
    s.buf = static_cast<std::uint8_t*>(std::calloc(bytes, 1));
    s.size = bytes;
    s.bits_unused = static_cast<int>(bytes * 8 - bits);
    return s;
}

NGSetupResponse_t build_ng_setup_response() {
    NGSetupResponse_t resp{};

    // id-AMFName (mandatory).
    AMFName_t amf_name{};
    amf_name.buf = reinterpret_cast<std::uint8_t*>(::strdup(kAmfName));
    amf_name.size = std::strlen(kAmfName);
    ::ngap::add_ie(resp.protocolIEs,
                ::ngap::make_ie(1 /* id-AMFName */, Criticality_reject, &asn_DEF_AMFName,
                              &amf_name));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_AMFName, &amf_name);

    // id-ServedGUAMIList (mandatory) -- one GUAMI, this lab's fixed PLMN + all-zero AMF Identifier.
    ServedGUAMIList_t guami_list{};
    auto* guami_item = static_cast<ServedGUAMIItem_t*>(std::calloc(1, sizeof(ServedGUAMIItem_t)));
    std::uint8_t plmn_bytes[3];
    encode_plmn_identity(kMcc, kMnc, plmn_bytes);
    guami_item->gUAMI.pLMNIdentity = make_octet_string(plmn_bytes, 3);
    guami_item->gUAMI.aMFRegionID = make_zero_bit_string(8);
    guami_item->gUAMI.aMFSetID = make_zero_bit_string(10);
    guami_item->gUAMI.aMFPointer = make_zero_bit_string(6);
    ASN_SEQUENCE_ADD(&guami_list.list, guami_item);
    ::ngap::add_ie(resp.protocolIEs,
                ::ngap::make_ie(96 /* id-ServedGUAMIList */, Criticality_reject,
                              &asn_DEF_ServedGUAMIList, &guami_list));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_ServedGUAMIList, &guami_list);

    // id-RelativeAMFCapacity (mandatory) -- arbitrary mid-range lab value (0..255 per spec range).
    RelativeAMFCapacity_t capacity = 128;
    ::ngap::add_ie(resp.protocolIEs,
                ::ngap::make_ie(86 /* id-RelativeAMFCapacity */, Criticality_ignore,
                              &asn_DEF_RelativeAMFCapacity, &capacity));

    // id-PLMNSupportList (mandatory) -- this lab's fixed PLMN + S-NSSAI (sst=1, sd=1).
    PLMNSupportList_t plmn_support{};
    auto* plmn_item = static_cast<PLMNSupportItem_t*>(std::calloc(1, sizeof(PLMNSupportItem_t)));
    plmn_item->pLMNIdentity = make_octet_string(plmn_bytes, 3);
    auto* slice_item = static_cast<SliceSupportItem_t*>(std::calloc(1, sizeof(SliceSupportItem_t)));
    slice_item->s_NSSAI.sST = make_octet_string(&kSst, 1);
    auto* sd = static_cast<SD_t*>(std::calloc(1, sizeof(SD_t)));
    const std::uint8_t sd_bytes[3] = {static_cast<std::uint8_t>((kSd >> 16) & 0xff),
                                      static_cast<std::uint8_t>((kSd >> 8) & 0xff),
                                      static_cast<std::uint8_t>(kSd & 0xff)};
    *sd = make_octet_string(sd_bytes, 3);
    slice_item->s_NSSAI.sD = sd;
    ASN_SEQUENCE_ADD(&plmn_item->sliceSupportList.list, slice_item);
    ASN_SEQUENCE_ADD(&plmn_support.list, plmn_item);
    ::ngap::add_ie(resp.protocolIEs,
                ::ngap::make_ie(80 /* id-PLMNSupportList */, Criticality_reject,
                              &asn_DEF_PLMNSupportList, &plmn_support));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PLMNSupportList, &plmn_support);

    return resp;
}

NGAP_PDU_t wrap_successful_outcome_ng_setup(NGSetupResponse_t response) {
    NGAP_PDU_t pdu{};
    pdu.present = NGAP_PDU_PR_successfulOutcome;
    pdu.choice.successfulOutcome = static_cast<SuccessfulOutcome_t*>(
        std::calloc(1, sizeof(SuccessfulOutcome_t)));
    pdu.choice.successfulOutcome->procedureCode = 21 /* id-NGSetup */;
    pdu.choice.successfulOutcome->criticality = Criticality_reject;
    pdu.choice.successfulOutcome->value.present = SuccessfulOutcome__value_PR_NGSetupResponse;
    pdu.choice.successfulOutcome->value.choice.NGSetupResponse = response;
    return pdu;
}

void handle_ng_setup_request(ngap_core::SctpSocket& assoc, const InitiatingMessage_t& msg) {
    (void)msg; // NGSetupRequest's IEs (GlobalRANNodeID, SupportedTAList, ...) aren't needed yet
               // to build this lab's fixed NGSetupResponse -- Stage 1 only proves NG Setup
               // succeeds, not per-gNB TA/slice negotiation.
    spdlog::info("amf-ngap: received NGSetupRequest, sending NGSetupResponse");

    NGSetupResponse_t response = build_ng_setup_response();
    NGAP_PDU_t pdu = wrap_successful_outcome_ng_setup(response);
    const auto bytes = ::ngap::encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &pdu);

    if (bytes.empty()) {
        spdlog::error("amf-ngap: failed to PER-encode NGSetupResponse");
        return;
    }
    {
        std::string hex;
        for (auto b : bytes) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02x", b);
            hex += buf;
        }
        spdlog::info("amf-ngap: NGSetupResponse hex: {}", hex);
    }
    assoc.send(bytes);
    spdlog::info("amf-ngap: sent NGSetupResponse ({} bytes)", bytes.size());
}

NGAP_PDU_t wrap_initiating_downlink_nas_transport(DownlinkNASTransport_t msg) {
    NGAP_PDU_t pdu{};
    pdu.present = NGAP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage = static_cast<InitiatingMessage_t*>(
        std::calloc(1, sizeof(InitiatingMessage_t)));
    pdu.choice.initiatingMessage->procedureCode = 4 /* id-DownlinkNASTransport */;
    pdu.choice.initiatingMessage->criticality = Criticality_ignore;
    pdu.choice.initiatingMessage->value.present = InitiatingMessage__value_PR_DownlinkNASTransport;
    pdu.choice.initiatingMessage->value.choice.DownlinkNASTransport = msg;
    return pdu;
}

// Shared DownlinkNASTransport send path -- Stage 2's AuthenticationRequest and Stage 4's
// SecurityModeCommand both just wrap a NAS-PDU byte string with the same two UE identifiers and
// send it, previously duplicated inline (see this function's call sites).
void send_downlink_nas_transport(ngap_core::SctpSocket& assoc, unsigned long amf_ue_id,
                                 unsigned long ran_ue_id, const std::vector<std::uint8_t>& nas_bytes) {
    NAS_PDU_t out_nas_pdu = make_octet_string(nas_bytes.data(), nas_bytes.size());
    AMF_UE_NGAP_ID_t amf_ue_ngap_id{};
    asn_ulong2INTEGER(&amf_ue_ngap_id, amf_ue_id);
    RAN_UE_NGAP_ID_t out_ran_ue_id = ran_ue_id;

    DownlinkNASTransport_t dl{};
    ::ngap::add_ie(dl.protocolIEs,
                ::ngap::make_ie(10 /* id-AMF-UE-NGAP-ID */, Criticality_reject,
                              &asn_DEF_AMF_UE_NGAP_ID, &amf_ue_ngap_id));
    ::ngap::add_ie(dl.protocolIEs,
                ::ngap::make_ie(85 /* id-RAN-UE-NGAP-ID */, Criticality_reject,
                              &asn_DEF_RAN_UE_NGAP_ID, &out_ran_ue_id));
    ::ngap::add_ie(dl.protocolIEs,
                ::ngap::make_ie(38 /* id-NAS-PDU */, Criticality_reject, &asn_DEF_NAS_PDU,
                              &out_nas_pdu));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_AMF_UE_NGAP_ID, &amf_ue_ngap_id);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NAS_PDU, &out_nas_pdu);

    NGAP_PDU_t pdu = wrap_initiating_downlink_nas_transport(dl);
    const auto bytes = ::ngap::encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &pdu);

    if (bytes.empty()) {
        spdlog::error("amf-ngap: failed to PER-encode DownlinkNASTransport");
        return;
    }
    assoc.send(bytes);
}

// Calls real AUSF's Nausf_UEAuthentication (POST .../ue-authentications), optionally carrying
// `resync_info` (TS 33.102 §6.3.3 SQN resynchronisation, ADR-0037 -- set when this call is a
// resync retry after an AuthenticationFailure, nullopt for a first attempt) and, on success,
// sends the resulting NAS AuthenticationRequest to the UE via DownlinkNASTransport. Shared by
// handle_initial_ue_message (the first attempt) and handle_uplink_nas_transport (the resync
// retry) -- the two call sites differ only in resync_info, everything else about initiating
// 5G-AKA is identical. Requires auth_state.supi/amf_ue_id/ran_ue_id already set. Returns false
// (having already logged why) on any failure; auth_state.confirmation_path/last_auth_rand are
// only updated on success.
bool initiate_5g_aka_authentication(
    ngap_core::SctpSocket& assoc, sbi_core::http2::Client& ausf_client,
    sbi_core::OAuth2Client& ausf_oauth, UeAuthState& auth_state,
    const std::optional<sbi_gen::ResynchronizationInfo_Nudm_UEAU>& resync_info) {
    auto token = ausf_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::error("amf-ngap: could not obtain AUSF bearer token: {}", token.error());
        return false;
    }

    sbi_gen::AuthenticationInfo req{};
    req.supiOrSuci = auth_state.supi;
    req.servingNetworkName = kServingNetworkName;
    req.resynchronizationInfo = resync_info;

    sbi_core::http2::ClientRequest http_req;
    http_req.method = "POST";
    http_req.url = std::string(kAusfBase) + "/nausf-auth/v1/ue-authentications";
    http_req.headers.emplace("content-type", "application/json");
    http_req.headers.emplace("authorization", "Bearer " + *token);
    http_req.body = nlohmann::json(req).dump();

    auto resp = ausf_client.send(http_req);
    if (!resp.has_value()) {
        spdlog::error("amf-ngap: AUSF call failed: {}", resp.error());
        return false;
    }
    if (resp->status != 201) {
        spdlog::error("amf-ngap: AUSF returned unexpected status {} for SUPI {}", resp->status,
                      auth_state.supi);
        return false;
    }

    sbi_gen::UEAuthenticationCtx ctx;
    try {
        ctx = nlohmann::json::parse(resp->body).get<sbi_gen::UEAuthenticationCtx>();
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("amf-ngap: AUSF returned a malformed UEAuthenticationCtx: {}", e.what());
        return false;
    }
    if (ctx.authType.value != sbi_gen::AuthType_Nausf_UEAuthentication::V5G_AKA) {
        spdlog::warn("amf-ngap: AUSF returned auth method '{}', only 5G_AKA is supported yet, "
                    "ignoring",
                    ctx.authType.value);
        return false;
    }

    // Stage 3 needs this to confirm the eventual AuthenticationResponse/Failure -- AUSF's own
    // response shape (_links["5g-aka"]["href"], nfs/ausf/src/main.cpp) is the source of truth for
    // the confirmation URL, not reconstructed from the auth context id ourselves.
    try {
        auth_state.confirmation_path = ctx._links.at("5g-aka").at("href").get<std::string>();
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("amf-ngap: AUSF's _links is missing the 5g-aka confirmation href: {}",
                      e.what());
        return false;
    }

    sbi_gen::Av5gAka av;
    try {
        av = ctx.n5gAuthData.get<sbi_gen::Av5gAka>();
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("amf-ngap: AUSF's n5gAuthData is not a valid Av5gAka: {}", e.what());
        return false;
    }
    const auto rand = aka_crypto::from_hex<16>(av.rand);
    const auto autn = aka_crypto::from_hex<16>(av.autn);
    if (!rand.has_value() || !autn.has_value()) {
        spdlog::error("amf-ngap: AUSF returned malformed hex RAND/AUTN");
        return false;
    }
    auth_state.last_auth_rand = *rand;  // needed if THIS AuthenticationRequest itself later resyncs

    const std::vector<std::uint8_t> auth_req_nas =
        amf::nas::encode_authentication_request(*rand, *autn, /*ngksi=*/0);
    send_downlink_nas_transport(assoc, auth_state.amf_ue_id, auth_state.ran_ue_id, auth_req_nas);
    spdlog::info("amf-ngap: sent DownlinkNASTransport with AuthenticationRequest ({} bytes), "
                "AMF-UE-NGAP-ID={}{}",
                auth_req_nas.size(), auth_state.amf_ue_id,
                resync_info.has_value() ? " (SQN resync retry)" : "");
    return true;
}

// Stage 2 (see docs/DECISIONS.md's staged NGAP/NAS plan): decodes the NAS RegistrationRequest
// carried in InitialUEMessage's NAS-PDU, calls real AUSF's Nausf_UEAuthentication
// (POST .../ue-authentications) with the extracted SUPI, and sends back a real NAS
// AuthenticationRequest (RAND/AUTN from AUSF's 5G-AKA vector) wrapped in DownlinkNASTransport.
// Stage 3 picks up from there (decoding the UE's AuthenticationResponse, confirming with AUSF,
// deriving KAMF) -- not handled here.
void handle_initial_ue_message(ngap_core::SctpSocket& assoc,
                               sbi_core::http2::Client& ausf_client,
                               sbi_core::OAuth2Client& ausf_oauth,
                               UeAuthState& auth_state,
                               const InitiatingMessage_t& msg) {
    const auto& container = msg.value.choice.InitialUEMessage.protocolIEs;

    const auto* nas_pdu_ie = ::ngap::find_ie(container, 38 /* id-NAS-PDU */);
    const auto* ran_ue_ngap_id_ie = ::ngap::find_ie(container, 85 /* id-RAN-UE-NGAP-ID */);
    if (nas_pdu_ie == nullptr || ran_ue_ngap_id_ie == nullptr) {
        spdlog::warn("amf-ngap: InitialUEMessage missing mandatory NAS-PDU/RAN-UE-NGAP-ID IE, "
                    "ignoring");
        return;
    }

    auto* nas_pdu = static_cast<NAS_PDU_t*>(::ngap::decode_ie_value(&asn_DEF_NAS_PDU, *nas_pdu_ie));
    auto* ran_ue_ngap_id = static_cast<RAN_UE_NGAP_ID_t*>(
        ::ngap::decode_ie_value(&asn_DEF_RAN_UE_NGAP_ID, *ran_ue_ngap_id_ie));
    if (nas_pdu == nullptr || ran_ue_ngap_id == nullptr) {
        spdlog::warn("amf-ngap: InitialUEMessage's NAS-PDU/RAN-UE-NGAP-ID failed to PER-decode, "
                    "ignoring");
        if (nas_pdu != nullptr) ASN_STRUCT_FREE(asn_DEF_NAS_PDU, nas_pdu);
        if (ran_ue_ngap_id != nullptr) ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, ran_ue_ngap_id);
        return;
    }
    spdlog::info("amf-ngap: InitialUEMessage decoded OK: RAN-UE-NGAP-ID={}, NAS-PDU {} bytes",
                *ran_ue_ngap_id, nas_pdu->size);

    const std::vector<std::uint8_t> nas_pdu_bytes(nas_pdu->buf, nas_pdu->buf + nas_pdu->size);
    const unsigned long ran_ue_id = *ran_ue_ngap_id;
    ASN_STRUCT_FREE(asn_DEF_NAS_PDU, nas_pdu);
    ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, ran_ue_ngap_id);

    const auto reg_info = amf::nas::decode_registration_request(nas_pdu_bytes);
    if (!reg_info.has_value()) {
        spdlog::warn("amf-ngap: InitialUEMessage's NAS-PDU is not a supported RegistrationRequest "
                    "(plain, null-scheme SUCI), ignoring");
        return;
    }
    spdlog::info("amf-ngap: InitialUEMessage from RAN-UE-NGAP-ID={}, SUPI={}", ran_ue_id,
                reg_info->supi);

    // Stage 4 reuses both IDs to send SecurityModeCommand on this same association once
    // authentication succeeds -- see UeAuthState's own comment.
    auth_state.amf_ue_id = g_next_amf_ue_ngap_id.fetch_add(1);
    auth_state.ran_ue_id = ran_ue_id;
    auth_state.ue_security_capability = reg_info->ue_security_capability;
    auth_state.supi = reg_info->supi;

    initiate_5g_aka_authentication(assoc, ausf_client, ausf_oauth, auth_state, std::nullopt);
}

// Stage 3: decodes the NAS-PDU carried in UplinkNASTransport (the UE's response to Stage 2's
// AuthenticationRequest), and for a real AuthenticationResponse, confirms RES* with real AUSF
// (PUT .../5g-aka-confirmation) and derives KAMF from the returned KSEAF. A real
// AuthenticationFailure (the outcome this project's currently-seeded test subscriber's fixed TS
// 35.207 SQN actually produces against a fresh UE -- see docs/DECISIONS.md ADR-0032) is decoded
// and, for a real SYNCH_FAILURE with AUTS, drives one real SQN resynchronisation retry (TS 33.102
// §6.3.3, ADR-0037): AUTS is forwarded to AUSF/UDM via resynchronizationInfo, and on success a
// fresh AuthenticationRequest is sent using the corrected vector, staying in
// AwaitingAuthenticationResponse to receive its response. Capped at one retry per association --
// see UeAuthState::sqn_resync_attempted's own comment.
void handle_uplink_nas_transport(ngap_core::SctpSocket& assoc, sbi_core::http2::Client& ausf_client,
                                 sbi_core::OAuth2Client& ausf_oauth,
                                 UeAuthState& auth_state,
                                 const InitiatingMessage_t& msg) {
    const auto nas_pdu_bytes_opt = extract_uplink_nas_pdu(msg);
    if (!nas_pdu_bytes_opt.has_value()) {
        return;
    }
    const auto& nas_pdu_bytes = *nas_pdu_bytes_opt;

    const auto outcome = amf::nas::decode_authentication_outcome(nas_pdu_bytes);
    if (!outcome.has_value()) {
        spdlog::warn("amf-ngap: UplinkNASTransport's NAS-PDU is not a supported "
                    "AuthenticationResponse/AuthenticationFailure, ignoring");
        return;
    }

    if (!outcome->success) {
        if (auth_state.sqn_resync_attempted || !outcome->auts.has_value() ||
            !auth_state.last_auth_rand.has_value()) {
            spdlog::warn("amf-ngap: UE sent AuthenticationFailure (mmCause=0x{:02x}{}) for SUPI "
                        "{} -- giving up ({})",
                        outcome->mm_cause, outcome->auts.has_value() ? ", with AUTS" : "",
                        auth_state.supi,
                        auth_state.sqn_resync_attempted
                            ? "resync already attempted once this association"
                        : !outcome->auts.has_value() ? "no AUTS to resync with"
                                                      : "no stored RAND to resync against");
            return;
        }

        spdlog::info("amf-ngap: UE sent AuthenticationFailure (mmCause=0x{:02x}, with AUTS) for "
                    "SUPI {} -- attempting SQN resynchronisation",
                    outcome->mm_cause, auth_state.supi);
        auth_state.sqn_resync_attempted = true;

        sbi_gen::ResynchronizationInfo_Nudm_UEAU resync_info{};
        resync_info.rand = aka_crypto::to_hex(*auth_state.last_auth_rand);
        resync_info.auts = aka_crypto::to_hex(*outcome->auts);

        initiate_5g_aka_authentication(assoc, ausf_client, ausf_oauth, auth_state, resync_info);
        return;  // stay in AwaitingAuthenticationResponse either way -- success just sent a fresh
                 // AuthenticationRequest; failure was already logged by
                 // initiate_5g_aka_authentication itself
    }

    if (auth_state.confirmation_path.empty()) {
        spdlog::warn("amf-ngap: received AuthenticationResponse with no pending authentication "
                    "context (out-of-order message or lost association state), ignoring");
        return;
    }

    auto token = ausf_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::error("amf-ngap: could not obtain AUSF bearer token: {}", token.error());
        return;
    }

    sbi_gen::ConfirmationData creq{};
    creq.resStar = aka_crypto::to_hex(outcome->res_star);

    sbi_core::http2::ClientRequest http_req;
    http_req.method = "PUT";
    http_req.url = std::string(kAusfBase) + auth_state.confirmation_path;
    http_req.headers.emplace("content-type", "application/json");
    http_req.headers.emplace("authorization", "Bearer " + *token);
    http_req.body = nlohmann::json(creq).dump();

    auto resp = ausf_client.send(http_req);
    if (!resp.has_value()) {
        spdlog::error("amf-ngap: AUSF confirmation call failed: {}", resp.error());
        return;
    }
    if (resp->status != 200) {
        spdlog::error("amf-ngap: AUSF confirmation returned unexpected status {} for SUPI {}",
                      resp->status, auth_state.supi);
        return;
    }

    sbi_gen::ConfirmationDataResponse cresp;
    try {
        cresp = nlohmann::json::parse(resp->body).get<sbi_gen::ConfirmationDataResponse>();
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("amf-ngap: AUSF returned a malformed ConfirmationDataResponse: {}", e.what());
        return;
    }
    if (cresp.authResult.value != sbi_gen::AuthResult::AUTHENTICATION_SUCCESS) {
        spdlog::warn("amf-ngap: AUSF confirmation reports authentication FAILURE for SUPI {} "
                    "(claimed RES* did not match XRES*)",
                    auth_state.supi);
        return;
    }
    if (!cresp.kseaf.has_value()) {
        spdlog::error("amf-ngap: AUSF confirmed success but returned no kseaf for SUPI {}",
                      auth_state.supi);
        return;
    }
    const auto kseaf = aka_crypto::from_hex<32>(*cresp.kseaf);
    if (!kseaf.has_value()) {
        spdlog::error("amf-ngap: AUSF returned malformed hex kseaf for SUPI {}", auth_state.supi);
        return;
    }

    // Same fixed ABBA=0x0000 this AMF sent in Stage 2's AuthenticationRequest -- KAMF only
    // converges with the UE's own derivation if both sides used the same ABBA value.
    const aka_crypto::Abba abba{0x00, 0x00};
    const auto kamf = aka_crypto::derive_kamf(*kseaf, strip_imsi_prefix(auth_state.supi), abba);
    spdlog::info("amf-ngap: authentication SUCCESSFUL for SUPI {}, KAMF derived", auth_state.supi);

    // Stage 4: activate NAS security. KNASenc/KNASint (TS 33.501 Annex A.8) for this project's
    // only implemented algorithm pair, 128-NEA2/128-NIA2 -- see aka_crypto/nas_security.hpp.
    auth_state.knas_int = aka_crypto::derive_knas_int(kamf, aka_crypto::kNia2AlgorithmIdentity);
    auth_state.knas_enc = aka_crypto::derive_knas_enc(kamf, aka_crypto::kNea2AlgorithmIdentity);

    const auto smc_nas =
        amf::nas::encode_security_mode_command(*auth_state.knas_int, auth_state.ue_security_capability,
                                               /*downlink_count=*/0);
    send_downlink_nas_transport(assoc, auth_state.amf_ue_id, auth_state.ran_ue_id, smc_nas);
    auth_state.phase = UeAuthState::Phase::AwaitingSecurityModeComplete;
    spdlog::info("amf-ngap: sent DownlinkNASTransport with SecurityModeCommand ({} bytes), "
                "AMF-UE-NGAP-ID={}",
                smc_nas.size(), auth_state.amf_ue_id);
}

// Stage 4 continued: decodes+verifies the NAS-PDU carried in the UplinkNASTransport that follows
// a sent SecurityModeCommand, expected to be a SecurityModeComplete (TS 24.501 §8.2.26). This is
// this project's first NAS message secured end-to-end (128-NIA2 MAC verified, 128-NEA2
// deciphered). On success, immediately continues into Stage 5: sends RegistrationAccept (the
// first message secured with the now-confirmed normal context, downlink_count=1), then -- since a
// real UE never acknowledges with RegistrationComplete for this build's minimal RegistrationAccept
// content (see UeAuthState::Phase's own comment) -- makes the real call this whole staged NGAP/NAS
// effort was for: Npcf_AMPolicyControl's CreateIndividualAMPolicyAssociation (TS 29.507), closing
// the loop ADR-0025->ADR-0029 left open. The resulting PolicyAssociation is stored in
// `ue_contexts` under this UE's SUPI.
void handle_uplink_nas_transport_smc_complete(ngap_core::SctpSocket& assoc,
                                              sbi_core::http2::Client& pcf_client,
                                              sbi_core::OAuth2Client& pcf_oauth,
                                              UeContextStore& ue_contexts, UeAuthState& auth_state,
                                              const InitiatingMessage_t& msg) {
    const auto nas_pdu_bytes_opt = extract_uplink_nas_pdu(msg);
    if (!nas_pdu_bytes_opt.has_value()) {
        return;
    }

    if (!auth_state.knas_int.has_value() || !auth_state.knas_enc.has_value()) {
        spdlog::warn("amf-ngap: received a post-SecurityModeCommand UplinkNASTransport with no "
                    "NAS security context (out-of-order message or lost association state), "
                    "ignoring");
        return;
    }

    const auto outcome = amf::nas::decode_security_mode_complete(
        *auth_state.knas_int, *auth_state.knas_enc, /*uplink_count=*/0, *nas_pdu_bytes_opt);
    if (!outcome.has_value()) {
        spdlog::warn("amf-ngap: UplinkNASTransport's NAS-PDU is not shaped like a "
                    "SecurityModeComplete, ignoring");
        return;
    }
    if (!outcome->mac_valid) {
        spdlog::warn("amf-ngap: SecurityModeComplete MAC verification FAILED for SUPI {} -- wrong "
                    "keys, a tampered/replayed message, or a NAS COUNT desync",
                    auth_state.supi);
        return;
    }

    spdlog::info("amf-ngap: SecurityModeComplete verified OK for SUPI {} -- NAS security context "
                "active",
                auth_state.supi);

    const auto reg_accept_nas =
        amf::nas::encode_registration_accept(*auth_state.knas_int, *auth_state.knas_enc,
                                             /*downlink_count=*/1);
    send_downlink_nas_transport(assoc, auth_state.amf_ue_id, auth_state.ran_ue_id, reg_accept_nas);
    spdlog::info("amf-ngap: sent DownlinkNASTransport with RegistrationAccept ({} bytes), "
                "AMF-UE-NGAP-ID={}",
                reg_accept_nas.size(), auth_state.amf_ue_id);

    auth_state.phase = UeAuthState::Phase::AwaitingPduSessionEstablishmentRequest;
    spdlog::info("amf-ngap: registration procedure complete for SUPI {} (no RegistrationComplete "
                "expected -- this build's RegistrationAccept carries no GUTI/NSSAI change, see "
                "UeAuthState::Phase's comment), requesting AM Policy Association from PCF",
                auth_state.supi);

    auto token = pcf_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::error("amf-ngap: could not obtain PCF bearer token: {}", token.error());
        return;
    }

    sbi_gen::PolicyAssociationRequest preq{};
    preq.supi = auth_state.supi;
    // Mandatory per TS 29.507's schema even though nothing here implements a receiver yet -- see
    // kSelfBase's own comment.
    preq.notificationUri = std::string(kSelfBase) + "/namf-callback/v1/am-policy-notify";
    // Mandatory (TS 29.571 §5.2.2 SupportedFeatures, a hex-encoded optional-feature bitmask) --
    // empty string means "none of PCF's optional features requested," the correct value given
    // this project doesn't implement any of them. Found via a real POST to PCF returning 400
    // "key 'suppFeat' not found" before this field was added -- not assumed correct from the
    // schema alone.
    preq.suppFeat = "";

    sbi_core::http2::ClientRequest http_req;
    http_req.method = "POST";
    http_req.url = std::string(kPcfBase) + "/npcf-am-policy-control/v1/policies";
    http_req.headers.emplace("content-type", "application/json");
    http_req.headers.emplace("authorization", "Bearer " + *token);
    http_req.body = nlohmann::json(preq).dump();

    auto resp = pcf_client.send(http_req);
    if (!resp.has_value()) {
        spdlog::error("amf-ngap: PCF CreateIndividualAMPolicyAssociation call failed: {}",
                      resp.error());
        return;
    }
    if (resp->status != 201) {
        spdlog::error("amf-ngap: PCF CreateIndividualAMPolicyAssociation returned unexpected "
                    "status {} for SUPI {}",
                    resp->status, auth_state.supi);
        return;
    }

    nlohmann::json association_json;
    try {
        association_json = nlohmann::json::parse(resp->body);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("amf-ngap: PCF returned a malformed PolicyAssociation: {}", e.what());
        return;
    }

    ue_contexts.put(auth_state.supi, association_json);
    spdlog::info("amf-ngap: AM Policy Association established with PCF for SUPI {} -- UE "
                "registration procedure fully complete",
                auth_state.supi);
}

// PDU Session Establishment (TS 23.502 §4.3.2.2.1), AMF's side of it: decodes+verifies the
// UlNasTransport carrying the UE's PDU Session Establishment Request, then makes the real call
// this project's SMF turn stood its API up for but never had a live trigger for until now:
// Nsmf_PDUSession's CreateSMContext (TS 29.502), multipart/related per SMF's own handler
// (nfs/smf/src/main.cpp requires it, matching TS 29.502's schema).
//
// Deliberately does NOT decode the actual 5GSM PDU Session Establishment Request payload
// container (see amf::nas::decode_ul_nas_transport's own comment) and, symmetrically, does NOT
// send anything back to the UE afterward: SMF's current CreateSMContext response
// (SmContextCreatedData) carries no n1SmMsg (nfs/smf/src/main.cpp only ever sets pduSessionId/
// sNssai on it -- its own disclosed scope, predating this turn), so there is no real PDU Session
// Establishment Accept content for AMF to forward. Synthesizing one here would mean AMF fabricating
// SM-layer decisions (PDU session type, QoS rules, session-AMBR) that are properly SMF's job --
// worse than disclosing the gap. This is this project's final implemented step of the procedure;
// the UE-visible Accept/Reject is a disclosed gap for a future SMF turn to close, not silently
// faked here.
void handle_uplink_nas_transport_pdu_session_establishment(sbi_core::http2::Client& smf_client,
                                                            sbi_core::OAuth2Client& smf_oauth,
                                                            const std::string& amf_instance_id,
                                                            UeAuthState& auth_state,
                                                            const InitiatingMessage_t& msg) {
    const auto nas_pdu_bytes_opt = extract_uplink_nas_pdu(msg);
    if (!nas_pdu_bytes_opt.has_value()) {
        return;
    }

    if (!auth_state.knas_int.has_value() || !auth_state.knas_enc.has_value()) {
        spdlog::warn("amf-ngap: received a post-registration UplinkNASTransport with no NAS "
                    "security context (out-of-order message or lost association state), ignoring");
        return;
    }

    // uplink_count=1: SecurityModeComplete was uplink_count=0, and (per UeAuthState::Phase's
    // comment) a real UE never sends RegistrationComplete for this build's minimal
    // RegistrationAccept, so this UlNasTransport is genuinely the second secured uplink message,
    // not the third -- confirmed via real interop.
    const auto outcome = amf::nas::decode_ul_nas_transport(*auth_state.knas_int, *auth_state.knas_enc,
                                                            /*uplink_count=*/1, *nas_pdu_bytes_opt);
    if (!outcome.has_value()) {
        spdlog::warn("amf-ngap: UplinkNASTransport's NAS-PDU is not shaped like a UlNasTransport "
                    "carrying N1 SM information, ignoring");
        return;
    }
    if (!outcome->mac_valid) {
        spdlog::warn("amf-ngap: UlNasTransport MAC verification FAILED for SUPI {} -- wrong keys, "
                    "a tampered/replayed message, or a NAS COUNT desync",
                    auth_state.supi);
        return;
    }
    auth_state.phase = UeAuthState::Phase::Done; // this project's only implemented SM procedure

    if (!outcome->dnn.has_value() || !outcome->snssai_sst.has_value()) {
        spdlog::error("amf-ngap: UlNasTransport for SUPI {} is missing DNN and/or S-NSSAI -- this "
                    "build requires both to call SMF (matches SMF's own CreateSMContext "
                    "requirement, see docs/DECISIONS.md ADR-0029), ignoring",
                    auth_state.supi);
        return;
    }

    spdlog::info("amf-ngap: PDU Session Establishment Request verified OK for SUPI {}, "
                "pduSessionId={}, dnn={} -- requesting SM context from SMF",
                auth_state.supi, outcome->pdu_session_id, *outcome->dnn);

    auto token = smf_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::error("amf-ngap: could not obtain SMF bearer token: {}", token.error());
        return;
    }

    sbi_gen::SmContextCreateData create_data{};
    create_data.servingNfId = amf_instance_id;
    create_data.servingNetwork.mcc = kMcc;
    create_data.servingNetwork.mnc = kMnc;
    create_data.anType.value = sbi_gen::AccessType::V3GPP_ACCESS;
    // Mandatory per TS 29.502's schema even though nothing here implements a receiver yet -- same
    // disclosed-deferred-callback shape as kSelfBase's other uses (PCF's notificationUri).
    create_data.smContextStatusUri =
        std::string(kSelfBase) + "/namf-callback/v1/sm-context-status";
    create_data.supi = auth_state.supi;
    create_data.pduSessionId = outcome->pdu_session_id;
    create_data.dnn = *outcome->dnn;
    sbi_gen::Snssai snssai{};
    snssai.sst = *outcome->snssai_sst;
    if (outcome->snssai_sd.has_value()) {
        char sd_hex[7];
        std::snprintf(sd_hex, sizeof(sd_hex), "%02x%02x%02x", (*outcome->snssai_sd)[0],
                     (*outcome->snssai_sd)[1], (*outcome->snssai_sd)[2]);
        snssai.sd = std::string(sd_hex);
    }
    create_data.sNssai = snssai;

    sbi_core::multipart::Part json_part;
    json_part.content_type = "application/json";
    json_part.body = nlohmann::json(create_data).dump();
    const auto encoded = sbi_core::multipart::encode({json_part});

    sbi_core::http2::ClientRequest http_req;
    http_req.method = "POST";
    http_req.url = std::string(kSmfBase) + "/nsmf-pdusession/v1/sm-contexts";
    http_req.headers.emplace("content-type", encoded.content_type_header);
    http_req.headers.emplace("authorization", "Bearer " + *token);
    http_req.body = encoded.body;

    auto resp = smf_client.send(http_req);
    if (!resp.has_value()) {
        spdlog::error("amf-ngap: SMF CreateSMContext call failed: {}", resp.error());
        return;
    }
    if (resp->status != 201) {
        spdlog::error("amf-ngap: SMF CreateSMContext returned unexpected status {} for SUPI {}",
                    resp->status, auth_state.supi);
        return;
    }

    spdlog::info("amf-ngap: SM context established with SMF for SUPI {}, pduSessionId={} -- PDU "
                "Session Establishment procedure complete (no N1 SM Accept to forward -- see "
                "this function's own disclosed-gap comment)",
                auth_state.supi, outcome->pdu_session_id);
}

void handle_association(ngap_core::SctpSocket assoc, sbi_core::http2::Client& ausf_client,
                        sbi_core::OAuth2Client& ausf_oauth, sbi_core::http2::Client& pcf_client,
                        sbi_core::OAuth2Client& pcf_oauth, sbi_core::http2::Client& smf_client,
                        sbi_core::OAuth2Client& smf_oauth, const std::string& amf_instance_id,
                        UeContextStore& ue_contexts) {
    spdlog::info("amf-ngap: gNB association established");
    UeAuthState auth_state{}; // this association's single UE, see UeAuthState's own comment
    while (true) {
        auto bytes = assoc.receive();
        if (bytes.empty()) {
            spdlog::info("amf-ngap: gNB association closed");
            return;
        }

        NGAP_PDU_t* pdu = ::ngap::decode_pdu(bytes);
        if (pdu == nullptr) {
            std::string hex;
            for (auto b : bytes) {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%02x", b);
                hex += buf;
            }
            spdlog::warn("amf-ngap: failed to decode NGAP PDU ({} bytes), ignoring: {}", bytes.size(), hex);
            continue;
        }

        if (pdu->present == NGAP_PDU_PR_initiatingMessage &&
            pdu->choice.initiatingMessage->procedureCode == 21 /* id-NGSetup */) {
            handle_ng_setup_request(assoc, *pdu->choice.initiatingMessage);
        } else if (pdu->present == NGAP_PDU_PR_initiatingMessage &&
                  pdu->choice.initiatingMessage->procedureCode == 15 /* id-InitialUEMessage */) {
            handle_initial_ue_message(assoc, ausf_client, ausf_oauth, auth_state,
                                      *pdu->choice.initiatingMessage);
        } else if (pdu->present == NGAP_PDU_PR_initiatingMessage &&
                  pdu->choice.initiatingMessage->procedureCode == 46 /* id-UplinkNASTransport */) {
            switch (auth_state.phase) {
            case UeAuthState::Phase::AwaitingAuthenticationResponse:
                handle_uplink_nas_transport(assoc, ausf_client, ausf_oauth, auth_state,
                                            *pdu->choice.initiatingMessage);
                break;
            case UeAuthState::Phase::AwaitingSecurityModeComplete:
                handle_uplink_nas_transport_smc_complete(assoc, pcf_client, pcf_oauth, ue_contexts,
                                                         auth_state, *pdu->choice.initiatingMessage);
                break;
            case UeAuthState::Phase::AwaitingPduSessionEstablishmentRequest:
                handle_uplink_nas_transport_pdu_session_establishment(
                    smf_client, smf_oauth, amf_instance_id, auth_state,
                    *pdu->choice.initiatingMessage);
                break;
            case UeAuthState::Phase::Done:
                spdlog::warn("amf-ngap: received an UplinkNASTransport after this association's "
                            "one PDU session was already established for SUPI {}, ignoring (out "
                            "of scope: no second PDU session or other post-establishment NAS "
                            "procedure implemented yet)",
                            auth_state.supi);
                break;
            }
        } else {
            spdlog::warn("amf-ngap: received NGAP PDU (present={}) with no handler yet, ignoring",
                        static_cast<int>(pdu->present));
        }

        ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, pdu);
    }
}

} // namespace

void run_ngap_lifecycle(const std::string& bind_address, unsigned short bind_port,
                        const std::string& amf_instance_id, const std::string& nrf_base,
                        UeContextStore& ue_contexts) {
    ngap_core::SctpSocket listener;
    listener.bind_and_listen(bind_address, bind_port);
    spdlog::info("amf-ngap: listening for NGAP/N2 (SCTP) on {}:{}", bind_address, bind_port);

    // AMF's own client identity + token source for calling AUSF -- a dedicated Client/OAuth2Client
    // pair living on this thread, not shared with run_nrf_lifecycle's or any future ioc-thread
    // one, since libs/sbi-core's http2::Client is synchronous/not thread-shared (same "separate
    // thread gets its own separate client" discipline run_nrf_lifecycle already established, see
    // docs/DECISIONS.md ADR-0006/ADR-0027).
    sbi_core::http2::TlsConfig ausf_client_tls{
        .cert_path = CERTS_DIR "/amf/cert.pem",
        .key_path = CERTS_DIR "/amf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client ausf_client(std::move(ausf_client_tls));
    sbi_core::OAuth2Client ausf_oauth(ausf_client, nrf_base + "/oauth2/token", amf_instance_id,
                                      "nausf-auth", "AUSF");

    // Same pattern, a second dedicated Client/OAuth2Client pair for PCF (Stage 5).
    sbi_core::http2::TlsConfig pcf_client_tls{
        .cert_path = CERTS_DIR "/amf/cert.pem",
        .key_path = CERTS_DIR "/amf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client pcf_client(std::move(pcf_client_tls));
    sbi_core::OAuth2Client pcf_oauth(pcf_client, nrf_base + "/oauth2/token", amf_instance_id,
                                     "npcf-am-policy-control", "PCF");

    // Same pattern, a third dedicated Client/OAuth2Client pair for SMF (PDU Session Establishment).
    sbi_core::http2::TlsConfig smf_client_tls{
        .cert_path = CERTS_DIR "/amf/cert.pem",
        .key_path = CERTS_DIR "/amf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    sbi_core::http2::Client smf_client(std::move(smf_client_tls));
    sbi_core::OAuth2Client smf_oauth(smf_client, nrf_base + "/oauth2/token", amf_instance_id,
                                     "nsmf-pdusession", "SMF");

    while (true) {
        ngap_core::SctpSocket assoc = listener.accept();
        // Single-association handling, sequentially, on this same thread -- this lab's scope is
        // one gNB/one UE at a time (see docs/DECISIONS.md ADR-0031); a real AMF would handle
        // multiple concurrent associations.
        handle_association(std::move(assoc), ausf_client, ausf_oauth, pcf_client, pcf_oauth,
                           smf_client, smf_oauth, amf_instance_id, ue_contexts);
    }
}

} // namespace amf::ngap
