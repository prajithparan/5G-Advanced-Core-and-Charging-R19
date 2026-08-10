#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "aka_crypto/kdf.hpp"

// Minimal NAS-5GS (TS 24.501) codec -- Stage 2+ of docs/DECISIONS.md's staged NGAP/NAS plan.
// Hand-rolled: NAS-5GS is TLV-encoded, not ASN.1, so no codegen tool applies the way NGAP's PER
// codec does (see ADR-0031 for why "hand-rolled partial parsing" was explicitly rejected for
// NGAP -- that concern doesn't transfer here, since there's no asn1c-equivalent for NAS-5GS to
// generate from in the first place; every real 5GC's NAS layer is hand-written for the same
// reason). Byte layouts below were derived by reading
// simulators/ransim/vendor/UERANSIM/src/lib/nas as a read-only reference oracle (arms-length per
// ADR-0016/ADR-0031 -- never copied, only read to confirm the real wire format against TS 24.501/
// TS 24.007's well-known message structures). Scope is exactly what this build's staged plan
// needs -- RegistrationRequest decode (SUCI extraction only, null protection scheme),
// AuthenticationRequest encode, AuthenticationResponse/AuthenticationFailure decode,
// SecurityModeCommand encode, and SecurityModeComplete decode -- not a general NAS parser.

namespace amf::nas {

struct RegistrationRequestInfo {
    // "imsi-<mcc><mnc><msin>", reconstructed directly from a null-protection-scheme SUCI's
    // plaintext scheme output -- see decode_registration_request's own comment for why this is a
    // faithful reconstruction, not a fabrication, and why it's the correct string to pass to
    // AUSF's supiOrSuci today.
    std::string supi;
    // Raw TLV *value* bytes (NOT including the 0x2E IEI byte) of the UE Security Capability IE
    // (TS 24.501 §9.11.3.54), captured verbatim so Stage 4's SecurityModeCommand can replay it
    // unmodified -- the UE rejects SMC with UE_SECURITY_CAP_MISMATCH otherwise (TS 24.501 §5.4.2,
    // the anti-bidding-down-attack check), confirmed against
    // simulators/ransim/vendor/UERANSIM/src/ue/nas/mm/security.cpp's own
    // `DeepEqualsIe(msg.replayedUeSecurityCapabilities, createSecurityCapabilityIe())`. This
    // project doesn't need to interpret the bitmap (which algorithms the UE supports) since it
    // always selects the same fixed pair (128-NEA2/128-NIA2, see aka_crypto/nas_security.hpp) --
    // byte-exact replay is all TS 24.501 requires here. Empty if the IE wasn't present (out of
    // scope: no real UE config in this project ever omits it, see
    // simulators/ransim/vendor/UERANSIM/src/ue/nas/mm/register.cpp's unconditional
    // `request->ueSecurityCapability = createSecurityCapabilityIe()`).
    std::vector<std::uint8_t> ue_security_capability;
};

// Returns std::nullopt (the caller logs why) if the NAS-PDU isn't a plain, unprotected
// RegistrationRequest carrying a null-protection-scheme SUCI encoding an IMSI-format SUPI --
// every other case (ciphered NAS, GUTI-based registration, a real protection scheme) is
// explicitly out of scope for this stage, not silently misparsed.
std::optional<RegistrationRequestInfo>
decode_registration_request(const std::vector<std::uint8_t>& nas_pdu);

// ngksi: 0-6, a fresh key set identifier this AMF is allocating for this authentication run (no
// prior security context exists yet, so any fresh value is correct; this lab's single-UE-at-a-time
// scope, see ADR-0031, makes a fixed value 0 unambiguous -- disclosed, not a spec-mandated value).
std::vector<std::uint8_t> encode_authentication_request(const std::array<std::uint8_t, 16>& rand,
                                                        const std::array<std::uint8_t, 16>& autn,
                                                        int ngksi);

// Result of decoding a NAS-PDU carried in UplinkNASTransport in response to a Stage 2
// AuthenticationRequest -- TS 24.501 defines exactly two real outcomes here (ignoring EAP, out of
// scope), AuthenticationResponse (success, carries RES*) or AuthenticationFailure (carries a
// cause and, for a SQN synchronization failure specifically, AUTS). Both are decoded here --
// not just the success path -- because a real UE talking to this project's currently-seeded test
// subscriber (see docs/DECISIONS.md ADR-0032) *always* hits the failure path on first contact
// (UDM's seeded SQN is TS 35.207 Test Set 1's fixed, large value, which legitimately exceeds any
// fresh UE's SQN-MS=0), so decoding it correctly (not crashing, not misparsing) matters in
// practice, not just in theory.
struct AuthenticationOutcome {
    bool success = false;
    // Valid iff success. TS 24.501 §9.11.3.17 AuthenticationResponseParameter -- RES*, 16 octets.
    std::array<std::uint8_t, 16> res_star{};
    // Valid iff !success. TS 24.501 §9.11.3.28 5GMM cause -- e.g. 0x14 MAC_FAILURE,
    // 0x15 SYNCH_FAILURE (numeric values confirmed against
    // simulators/ransim/vendor/UERANSIM/src/lib/nas/enums.hpp's EMmCause, not guessed).
    std::uint8_t mm_cause = 0;
    // Valid iff !success and the UE included one (TS 24.501 §9.11.3.1
    // AuthenticationFailureParameter) -- present for SYNCH_FAILURE, absent otherwise. 14 octets:
    // SQN xor AK (6) || MAC-S (8), confirmed against
    // simulators/ransim/vendor/UERANSIM/src/ue/nas/keys.cpp's CalculateAuts, not assumed from
    // general AKA knowledge alone. Resynchronization (TS 33.501, using this AUTS to have UDM
    // reissue a corrected-SQN vector) is NOT implemented -- disclosed gap, see ADR-0032. This
    // field is decoded and surfaced so a future stage can implement it without another NAS-codec
    // change, not because this stage acts on it.
    std::optional<std::array<std::uint8_t, 14>> auts;
};

// Returns std::nullopt (the caller logs why) for anything other than a plain, unprotected
// AuthenticationResponse carrying authenticationResponseParameter (RES*) or a plain
// AuthenticationFailure -- e.g. an EAP-only AuthenticationResponse is out of scope (this project
// only implements the 5G-AKA path, not EAP-AKA' end-to-end over NAS).
std::optional<AuthenticationOutcome>
decode_authentication_outcome(const std::vector<std::uint8_t>& nas_pdu);

// Encodes a SecurityModeCommand (TS 24.501 §8.2.25, network->UE), integrity-protected only (never
// ciphered -- the UE cannot yet be assumed to trust the new KNASenc when it first receives this
// message, confirmed against
// simulators/ransim/vendor/UERANSIM/src/ue/nas/enc.cpp's own MakeSecurityHeaderType always
// returning INTEGRITY_PROTECTED_WITH_NEW_SECURITY_CONTEXT for this message type regardless of the
// selected ciphering algorithm), secured with 128-NIA2 over knas_int. Only the mandatory IEs this
// stage's single-UE, single-algorithm-pair happy path needs are encoded --
// selectedNasSecurityAlgorithms (fixed to 128-NEA2/128-NIA2, this project's only implemented pair),
// a fresh ngKSI=0, and ue_security_capability replayed verbatim (see RegistrationRequestInfo's own
// comment). Optional IEs (imeiSvRequest, EPS algorithms, additional5GSecurityInformation,
// eapMessage, abba, replayedS1UeNetworkCapability) are out of scope -- disclosed simplification,
// not silently dropped support for a case this build actually hits.
//
// downlink_count: this association's NAS downlink COUNT for this message. This project's
// single-registration-per-association scope (ADR-0031) means this is always the first secured
// downlink message, so callers always pass 0 -- kept as an explicit parameter (not hardcoded)
// so a future multi-message-per-association stage doesn't need a signature change.
std::vector<std::uint8_t>
encode_security_mode_command(const aka_crypto::NasIntKey& knas_int,
                             const std::vector<std::uint8_t>& ue_security_capability,
                             std::uint32_t downlink_count);

struct SecurityModeCompleteOutcome {
    // False means the MAC genuinely didn't verify (wrong keys, a tampered/replayed message, or a
    // NAS COUNT desync) -- a real, caller-visible outcome, not a decode failure. A decode failure
    // (message too short/wrong EPD/wrong security header type to even be a candidate) is instead
    // std::nullopt from decode_security_mode_complete itself, so the two are never conflated.
    bool mac_valid = false;
};

// Verifies MAC (128-NIA2) and deciphers (128-NEA2) a NAS-PDU carried in UplinkNASTransport,
// expected to be a SecurityModeComplete (TS 24.501 §8.2.26) responding to
// encode_security_mode_command's output. uplink_count mirrors downlink_count's own doc comment --
// always 0 in this project's current scope.
std::optional<SecurityModeCompleteOutcome>
decode_security_mode_complete(const aka_crypto::NasIntKey& knas_int,
                              const aka_crypto::NasEncKey& knas_enc,
                              std::uint32_t uplink_count,
                              const std::vector<std::uint8_t>& nas_pdu);

// Encodes a RegistrationAccept (TS 24.501 §8.2.7, network->UE), integrity-protected AND ciphered
// (the normal secured-message case, not SecurityModeCommand's "new security context" variant --
// by this point the NAS security context is established and confirmed, not being bootstrapped).
// Only the one mandatory IE (registrationResult) this stage's minimal happy path needs is
// encoded -- see encode_registration_accept's own definition comment for the disclosed IEs left
// out (GUTI reassignment, allowed NSSAI, etc.).
//
// downlink_count: this association's second secured downlink message (the first was
// SecurityModeCommand's downlink_count=0) -- callers always pass 1 in this project's current
// single-registration-per-association scope, kept explicit for the same reason
// encode_security_mode_command's downlink_count parameter is.
std::vector<std::uint8_t> encode_registration_accept(const aka_crypto::NasIntKey& knas_int,
                                                     const aka_crypto::NasEncKey& knas_enc,
                                                     std::uint32_t downlink_count);

struct RegistrationCompleteOutcome {
    // Same split as SecurityModeCompleteOutcome::mac_valid -- see that struct's own comment.
    bool mac_valid = false;
};

// Verifies MAC and deciphers a NAS-PDU carried in UplinkNASTransport, expected to be a
// RegistrationComplete (TS 24.501 §8.2.6) responding to encode_registration_accept's output --
// the final message of the registration procedure this project's staged NGAP/NAS plan targets.
// uplink_count: this association's second secured uplink message (the first was
// SecurityModeComplete's uplink_count=0) -- callers always pass 1 in this project's current scope.
//
// NOT CURRENTLY CALLED by any production handler (nfs/amf/src/ngap_task.cpp): confirmed via real
// interop and by reading UERANSIM's own receiveInitialRegistrationAccept
// (simulators/ransim/vendor/UERANSIM/src/ue/nas/mm/register.cpp:346-426) that a real UE only
// sends RegistrationComplete if RegistrationAccept carried a 5G-GUTI, an NSSCI=CHANGED
// indication, or a configuredNSSAI -- encode_registration_accept sends none of those (disclosed
// simplification, see its own comment), so this decoder is currently unreachable in practice.
// Kept (unit-tested, spec-correct) for when a future turn adds GUTI reassignment.
std::optional<RegistrationCompleteOutcome>
decode_registration_complete(const aka_crypto::NasIntKey& knas_int,
                             const aka_crypto::NasEncKey& knas_enc,
                             std::uint32_t uplink_count,
                             const std::vector<std::uint8_t>& nas_pdu);

struct UlNasTransportInfo {
    // Same split as SecurityModeCompleteOutcome::mac_valid -- see that struct's own comment.
    bool mac_valid = false;
    // Valid iff mac_valid. TS 24.501 §9.11.3.41 5GS PDU Session Identity 2. 0 (never a valid
    // assigned value, TS 24.501 range is 1-15) means the UE didn't include it -- out of scope:
    // this project's only implemented SM procedure (PDU Session Establishment) always includes
    // it, per simulators/ransim/vendor/UERANSIM/src/ue/nas/sm/transport.cpp's own unconditional
    // `ulTransport.pduSessionId = ...`.
    std::uint8_t pdu_session_id = 0;
    // TS 24.501 §9.11.2.1a (DNN), decoded from TS 23.003 §9.1's label-length-prefixed APN
    // encoding back into a dotted string (e.g. "internet"), confirmed against
    // simulators/ransim/vendor/UERANSIM/src/lib/nas/utils.cpp's own DnnFromApn (the exact inverse
    // operation). std::nullopt if the UE didn't include it.
    std::optional<std::string> dnn;
    // TS 24.501 §9.11.2.8 (S-NSSAI) -- only the SST(+SD) case is decoded (this project's only
    // configured S-NSSAI shape, sst=1/sd=1, see simulators/ransim/config/gnb.yaml); the
    // mapped-HPLMN-S-NSSAI length variants (TS 24.501's own IESNssai::Decode has five, this
    // decodes one) are out of scope, disclosed rather than silently misparsed -- an S-NSSAI with
    // a different length just leaves these std::nullopt.
    std::optional<std::uint8_t> snssai_sst;
    std::optional<std::array<std::uint8_t, 3>> snssai_sd;
    // The opaque 5GSM payload container bytes (the PDU Session Establishment Request itself), NOT
    // decoded here (see this function's own comment for why) but captured verbatim so the caller
    // can forward them to SMF as SmContextCreateData.n1SmMsg, TS 29.502's real mechanism for
    // getting the actual 5GSM content to the NF that's allowed to understand it. Added by
    // docs/DECISIONS.md ADR-0038 -- previously this project discarded these bytes entirely.
    std::vector<std::uint8_t> payload_container;
};

// Verifies MAC and deciphers a NAS-PDU carried in UplinkNASTransport, expected to be a
// UlNasTransport (TS 24.501 §8.2.10) wrapping N1 SM information (payloadContainerType=1) -- this
// project's only implemented post-registration NAS procedure, PDU Session Establishment
// (TS 23.502 §4.3.2.2.1). The actual 5GSM payload container bytes (the PDU Session Establishment
// Request itself) are NOT decoded here -- TS 24.501's whole point of the payload-container
// mechanism is that AMF routes SM messages without understanding their contents; only SMF decodes
// real 5GSM content. The container bytes are still captured verbatim (UlNasTransportInfo::
// payload_container, ADR-0038) so the caller can forward them to SMF unmodified, matching the real
// mechanism: AMF stays opaque to the content, SMF is the only NF that parses it. The
// transport-level optional IEs that determine WHERE to route the request (PDU session ID, S-NSSAI,
// DNN) are additionally extracted; requestType/oldPduSessionId/additionalInformation are walked
// past (skipped) to reach them but not surfaced -- unused by SMF's current CreateSMContext handler.
//
// uplink_count: this association's second secured uplink message (SecurityModeComplete=0; no
// RegistrationComplete is ever sent, see UeAuthState::Phase's own comment in ngap_task.cpp) --
// callers pass 1 in this project's current single-PDU-session scope.
std::optional<UlNasTransportInfo> decode_ul_nas_transport(const aka_crypto::NasIntKey& knas_int,
                                                          const aka_crypto::NasEncKey& knas_enc,
                                                          std::uint32_t uplink_count,
                                                          const std::vector<std::uint8_t>& nas_pdu);

// Encodes a secured DlNasTransport (TS 24.501 §8.2.9) wrapping opaque N1 SM information
// (payloadContainerType=1) -- AMF's delivery vehicle for the PDU Session Establishment Accept SMF
// builds and sends via Namf_Communication's N1N2MessageTransfer (ADR-0038). AMF does not decode
// n1_sm_container -- same opaque-payload-container discipline as decode_ul_nas_transport's own
// comment, just in the downlink direction. Byte layout confirmed against UERANSIM's real
// DlNasTransport::onBuild (simulators/ransim/vendor/UERANSIM/src/lib/nas/msg.cpp): mandatory
// payloadContainerType (Type-1) + payloadContainer (Type-6 LV-E) + optional pduSessionId
// (Type-3 TV, IEI 0x12 -- same IEI value UlNasTransport uses for the same IE).
//
// downlink_count: this association's second secured downlink message (SecurityModeCommand=0,
// RegistrationAccept=1) -- callers pass 2 in this project's current single-PDU-session scope.
std::vector<std::uint8_t> encode_dl_nas_transport(const aka_crypto::NasIntKey& knas_int,
                                                  const aka_crypto::NasEncKey& knas_enc,
                                                  std::uint32_t downlink_count,
                                                  std::uint8_t pdu_session_id,
                                                  const std::vector<std::uint8_t>& n1_sm_container);

} // namespace amf::nas
