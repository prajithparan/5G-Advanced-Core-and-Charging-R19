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
// A.7 (KAMF) is AMF/SEAF's own derivation, out of scope here -- AUSF's job stops at KSEAF.

namespace aka_crypto {

using Kausf = std::array<uint8_t, 32>;
using Kseaf = std::array<uint8_t, 32>;
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

}  // namespace aka_crypto
