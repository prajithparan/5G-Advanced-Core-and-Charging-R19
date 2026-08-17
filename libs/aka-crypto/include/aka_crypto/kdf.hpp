#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "aka_crypto/milenage.hpp"

// 3GPP generic KDF (TS 33.220 Annex B.2.0, HMAC-SHA-256 based) and the specific 5G-AKA / 5G
// EAP-AKA' key derivations built on it from TS 33.501 Annex A. FC values and per-derivation
// parameter lists (P0/L0, P1/L1, ...) were cross-checked this turn against free5GC's
// (github.com/free5gc/util/ueauth, github.com/free5gc/udm -- Apache-2.0, real spec-conformant
// open source, consulted as reference reading only per CLAUDE.md's build-vs-fork policy, not
// vendored or copied) rather than trusted from memory -- see docs/DECISIONS.md ADR-0026.
//
// Clause map (TS 33.501 Annex A):
//   A.2  KAUSF          FC=0x6A  KDF(CK||IK, SNN, SQN xor AK)
//   A.3  CK'/IK'        FC=0x20  KDF(CK||IK, SNN, SQN xor AK)   (5G EAP-AKA' only)
//   A.4  RES*/XRES*     FC=0x6B  KDF(CK||IK, SNN, RAND, RES)
//   A.5  HRES*/HXRES*   SHA-256(RAND || RES*), leftmost 128 bits (not the generic KDF)
//   A.6  KSEAF          FC=0x6C  KDF(KAUSF, SNN)
//   A.7  KAMF           FC=0x6D  KDF(KSEAF, SUPI, ABBA) -- AMF/SEAF's own derivation, added when
//                        nfs/amf actually needed it (Stage 3 of docs/DECISIONS.md's staged
//                        NGAP/NAS plan). FC value and parameter list (SUPI then ABBA, both KDF
//                        "P||L"-encoded like every other param above) cross-checked against
//                        simulators/ransim/vendor/UERANSIM/src/ue/nas/keys.cpp's own
//                        DeriveKeysSeafAmf (`CalculateKdfKey(kSeaf, 0x6D, s2, 2)` with
//                        `s2 = [EncodeKdfString(supi), abba]`) -- read-only reference oracle per
//                        ADR-0016/ADR-0031, not vendored/copied, same disclosure pattern as
//                        ADR-0026's KAUSF-for-EAP-AKA' reconstruction: this is a best-available
//                        reconstruction cross-checked against a real, independent implementation,
//                        not a confirmed citation against normative TS 33.501 text (this repo has
//                        no local copy of that spec's actual clause text, only OpenAPI YAML).
//   A.8  KNASenc/KNASint FC=0x69  KDF(KAMF, algorithm-type-distinguisher, algorithm-identity),
//                        rightmost 128 bits of the 256-bit KDF output. Same reconstruction-not-
//                        citation disclosure as A.7, cross-checked against
//                        simulators/ransim/vendor/UERANSIM/src/ue/nas/keys.cpp's own
//                        DeriveNasKeys (`CalculateKdfKey(kAmf, 0x69, s1, 2)` with
//                        `s1 = [FromOctet(N_NAS_enc_alg=0x01), FromOctet(ciphering_alg_id)]`, and
//                        the mirror for `N_NAS_int_alg=0x02`/integrity) and its own
//                        `.subCopy(16, 16)` (rightmost half of the 32-byte HMAC-SHA-256 output).
//   A.17 SoR-MAC-IAUSF   FC=0x77  KDF(KAUSF, SoR header, CounterSoR, [Steering Info List]),
//                        128 LSBs (rightmost 16 bytes) of the 256-bit KDF output. UNLIKE A.7/A.8
//                        above, this one IS a direct citation, not a reconstruction: confirmed
//                        2026-08-17 against a real local copy of 3GPP TS 33.501 v19.6.0 (Release
//                        19, matching this project's own target release), Annex A.17 (page 242)
//                        and clause 6.14.2.3 (CounterSoR state machine, page 122-123) -- gap-
//                        closure (docs/CAPABILITY_GAP_ANALYSIS.md task #104, ADR-0081). The P2
//                        (Steering Info List) parameter is genuinely optional in the KDF itself,
//                        included only when the caller's own Nausf_SoRProtection request supplied
//                        one.
//   A.18 SoR-MAC-IUE/    FC=0x78  KDF(KAUSF, 0x01, CounterSoR), 128 LSBs. Same real citation as
//        SoR-XMAC-IUE    A.17 above. One function computes both -- which name applies depends only
//                        on who computed it (UE sends SoR-MAC-IUE; AUSF pre-computes and caches
//                        the same value as SoR-XMAC-IUE to compare against).
//   A.9  KgNB/KN3IWF/... FC=0x6E  KDF(KAMF, uplink NAS COUNT, access type distinguisher). Real
//                        citation, confirmed 2026-08-17 against the same local TS 33.501 v19.6.0
//                        copy A.17/A.18 above cite (page 239) -- gap-closure (docs/
//                        CAPABILITY_GAP_ANALYSIS.md task #100, ADR-0090). This project only ever
//                        derives the 3GPP-access variant (KgNB), access type distinguisher fixed
//                        at 0x01 (Table A.9-1); the non-3GPP KN3IWF/KWAGF/KTNGF/KTWIF variants
//                        (distinguisher 0x02) are real but unbuilt, this project having no
//                        non-3GPP access NF.
//   A.10 NH              FC=0x6F  KDF(KAMF, SYNC-input). Real citation, same source, page 240.
//                        SYNC-input is the just-derived KgNB for the first NH in a chain, or the
//                        previous NH for every subsequent one -- this project's own disclosed gap
//                        (no NGAP InitialContextSetup exists yet, so no prior AS security context
//                        or NH chain is ever established for any UE before a handover) means every
//                        call this project makes derives chain position 0 (SYNC-input=KgNB,
//                        NCC=0), never a later position; see nfs/amf/src/ngap_task.cpp's own
//                        handle_path_switch_request for the real, disclosed caller-side detail.
//
// TS 33.503 Annex A (5G ProSe, a DIFFERENT spec document from TS 33.501 above -- gap-closure
// docs/CAPABILITY_GAP_ANALYSIS.md task #104, ADR-0091). Real citations, confirmed 2026-08-17
// against a real local copy of 3GPP TS 33.503 v19.3.0 (Release 19, matching this project's own
// target release -- the FIRST spec citation in this file that needed no version-gap disclosure):
//   A.2  CP-PRUK          FC=0x85  KDF(KAUSF_P, SUPI, relay service code), page 71. KAUSF_P is
//                         real KAUSF (aka_crypto::derive_kausf, TS 33.501 Annex A.2, FC=0x6A) from
//                         a normal EAP-AKA' run -- TS 33.503's own clause 6.1.3.2 (paraphrased,
//                         not the KDF itself) states KAUSF_P "is obtained in the same way as
//                         KAUSF is obtained for EAP-AKA' in clause 6.1.3.1 in TS 33.501" -- no new
//                         primary-authentication crypto, just a new label for an existing
//                         derivation's output when used for ProSe.
//   A.3  CP-PRUK ID*      FC=0x86  KDF(KAUSF_P, "PRUK-ID", relay service code, SUPI), page 71.
//   A.4  KNR_ProSe        FC=0x87  KDF(CP-PRUK, Nonce_2, Nonce_1), page 72.
// relay service code is encoded as 3 bytes (big-endian), the real, explicit width TS 33.503's own
// Annex A.5 states for the same parameter ("L1 = length of RSC (i.e. 0x00 0x03)") -- A.2/A.3
// don't restate the byte count themselves, so A.5's own explicit citation is used rather than
// guessed.

namespace aka_crypto {

using Kausf = std::array<uint8_t, 32>;
using Kseaf = std::array<uint8_t, 32>;
using Kamf = std::array<uint8_t, 32>;
using Abba = std::array<uint8_t, 2>;
using NasEncKey = std::array<uint8_t, 16>;
using NasIntKey = std::array<uint8_t, 16>;
using ResStar = std::array<uint8_t, 16>;
using HxresStar = std::array<uint8_t, 16>;
using CkPrime = std::array<uint8_t, 16>;
using IkPrime = std::array<uint8_t, 16>;

// Generic KDF per TS 33.220 Annex B.2.0: HMAC-SHA-256(key, FC || P0 || L0 || P1 || L1 || ...)
// where each Li is the 2-byte big-endian length of the preceding Pi. Returns the full 32-byte
// HMAC-SHA-256 output.
std::array<uint8_t, 32> generic_kdf(const std::vector<uint8_t>& key,
                                    uint8_t fc,
                                    const std::vector<std::vector<uint8_t>>& params);

Ak48 sqn_xor_ak(const Sqn& sqn, const Ak48& ak);

Kausf derive_kausf(const Key128& ck,
                   const Key128& ik,
                   const std::string& serving_network_name,
                   const Ak48& sqn_xor_ak);

std::pair<CkPrime, IkPrime> derive_ck_ik_prime(const Key128& ck,
                                               const Key128& ik,
                                               const std::string& serving_network_name,
                                               const Ak48& sqn_xor_ak);

ResStar derive_res_star(const Key128& ck,
                        const Key128& ik,
                        const std::string& serving_network_name,
                        const Key128& rand,
                        const Res64& res);

// Same TS 33.501 Annex A.5 formula computes both HRES* (SEAF, from the UE's RES*) and HXRES*
// (AUSF, from UDM's XRES*) -- one function, two call sites.
HxresStar derive_hxres_star(const Key128& rand, const ResStar& res_star);

Kseaf derive_kseaf(const Kausf& kausf, const std::string& serving_network_name);

// supi: the BARE identity digits (e.g. "999700000000001"), NOT this project's usual SBI-JSON
// "imsi-999700000000001" string representation -- confirmed via real interop after the two
// diverged and produced a KAMF a real UE could never converge on (see
// nfs/amf/src/ngap_task.cpp's strip_imsi_prefix and its own comment, ADR-0037). Callers holding
// the "imsi-"-prefixed form must strip it before calling this.
// abba: the same ABBA value sent to the UE in the NAS AuthenticationRequest (this project fixes
// it at 0x0000, see nfs/amf/src/nas_codec.hpp) -- the UE derives KAMF from the ABBA it actually
// received, so this must match exactly for the two sides' KAMF to converge.
Kamf derive_kamf(const Kseaf& kseaf, const std::string& supi, const Abba& abba);

// TS 33.501 Annex A.8 algorithm-type distinguisher values (P0), as reconstructed from UERANSIM's
// keys.cpp -- see this file's own top comment.
constexpr uint8_t kNasEncAlgorithmDistinguisher = 0x01;
constexpr uint8_t kNasIntAlgorithmDistinguisher = 0x02;

// TS 33.401 Annex B (referenced by TS 33.501 for the 5G algorithm set) algorithm identity values
// for 128-NEA2/128-NIA2 (AES-based) -- the only algorithm pair this project implements (see
// aka_crypto/nas_security.hpp), matching this project's fixed algorithm selection.
constexpr uint8_t kNea2AlgorithmIdentity = 0x02;
constexpr uint8_t kNia2AlgorithmIdentity = 0x02;

// algorithm_identity: the P1 parameter -- this project always passes kNea2AlgorithmIdentity /
// kNia2AlgorithmIdentity (its only implemented algorithm pair), but the parameter is kept
// explicit rather than hardcoded inside the function so a future algorithm addition doesn't need
// a second near-identical function.
NasEncKey derive_knas_enc(const Kamf& kamf, uint8_t algorithm_identity);
NasIntKey derive_knas_int(const Kamf& kamf, uint8_t algorithm_identity);

using SorMac = std::array<uint8_t, 16>;

// TS 33.501 Annex A.17 (real citation, see this file's own header comment). sor_header is the
// caller-supplied, already-encoded SOR header octets (TS 24.501 §9.11.3.51 -- this function
// treats them as opaque, does not construct or interpret them). counter_sor is the real,
// persistent per-KAUSF freshness counter (see clause 6.14.2.3 -- caller's responsibility to
// maintain per its own real state-machine rules, not this function's). steering_info_list is the
// optional P2 parameter, nullptr when the caller's own request had none to include.
SorMac derive_sor_mac_iausf(const Kausf& kausf,
                            const std::vector<uint8_t>& sor_header,
                            uint16_t counter_sor,
                            const std::vector<uint8_t>* steering_info_list);

// TS 33.501 Annex A.18 (real citation). Same formula computes both SoR-MAC-IUE (sent by the UE)
// and SoR-XMAC-IUE (pre-computed and cached by the AUSF to compare against) -- which name applies
// is a caller-side bookkeeping distinction, not a functional one.
SorMac derive_sor_mac_iue(const Kausf& kausf, uint16_t counter_sor);

using Kgnb = std::array<uint8_t, 32>;
using NextHopKey = std::array<uint8_t, 32>;

// TS 33.501 Annex A.9 Table A.9-1 access type distinguisher values (real citation, see this
// file's own header comment). This project only ever derives the 3GPP-access variant.
constexpr uint8_t kAccessType3gpp = 0x01;

// TS 33.501 Annex A.9 (real citation). uplink_nas_count is the real, persistent per-UE value
// (UeSecurityContext::uplink_count in nfs/amf's own store) at the moment this KgNB is derived --
// the same COUNT value a real AS security context establishment would use.
Kgnb derive_kgnb(const Kamf& kamf, uint32_t uplink_nas_count, uint8_t access_type_distinguisher);

// TS 33.501 Annex A.10 (real citation). sync_input is the just-derived KgNB (32 bytes) for the
// first NH in a chain, or the previous NH (32 bytes) for every subsequent one -- see this file's
// own header comment for this project's own disclosed real scope (chain position 0 only).
NextHopKey derive_nh(const Kamf& kamf, const std::array<uint8_t, 32>& sync_input);

using CpPruk = std::array<uint8_t, 32>;
using CpPrukIdStar = std::array<uint8_t, 32>;
using KnrProse = std::array<uint8_t, 32>;

// TS 33.503 Annex A.2 (real citation, see this file's own header comment). kausf_p: real KAUSF
// (aka_crypto::derive_kausf) from a normal EAP-AKA' run, relabeled per TS 33.503's own clause
// 6.1.3.2. supi: the bare identity digits (same convention as derive_kamf's own supi parameter --
// see its comment). relay_service_code: TS 24.554's RSC, encoded as 3 bytes big-endian (real
// width, TS 33.503 Annex A.5's own explicit citation, see this file's header comment).
CpPruk derive_cp_pruk(const Kausf& kausf_p, const std::string& supi, int64_t relay_service_code);

// TS 33.503 Annex A.3 (real citation). Same kausf_p/relay_service_code/supi as derive_cp_pruk
// above -- a distinct KDF output (FC=0x86, different P0/P-order), not a truncation of CP-PRUK.
CpPrukIdStar
derive_cp_pruk_id_star(const Kausf& kausf_p, const std::string& supi, int64_t relay_service_code);

// TS 33.503 Annex A.4 (real citation). nonce1/nonce2: the real Nonce_1 (from the ProSe
// authentication requester's own ProSeAuthenticationInfo.nonce1) and Nonce_2 (freshly generated
// by this AUSF, TS 33.503's own step 11 "AUSF...shall generate Nonce_2") -- both caller-supplied
// opaque byte strings, this function does not construct or interpret them further.
KnrProse derive_knr_prose(const CpPruk& cp_pruk,
                          const std::vector<uint8_t>& nonce1,
                          const std::vector<uint8_t>& nonce2);

} // namespace aka_crypto
