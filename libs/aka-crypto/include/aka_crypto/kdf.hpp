#pragma once

#include "aka_crypto/milenage.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

}  // namespace aka_crypto
