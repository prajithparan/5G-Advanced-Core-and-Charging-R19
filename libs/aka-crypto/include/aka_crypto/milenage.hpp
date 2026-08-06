#pragma once

#include <array>
#include <cstdint>

// MILENAGE algorithm set (3GPP TS 35.205/35.206), the example authentication and key generation
// functions f1, f2, f3, f4, f5 built on AES-128 (Rijndael), as used by UDM/ARPF (nfs/udm) and
// verified by AUSF-role test clients (tests/integration) that stand in for a UE/USIM (no real
// SIM/USIM exists in this project). Implemented from the public TS 35.206 algorithm definition
// (a well-defined, non-ambiguous construction over AES-128 -- not an area where the spec is
// underdetermined), independently re-derived in C++, then verified in
// tests/conformance/test_milenage.cpp against 3GPP TS 35.207 Test Set 1 -- a real, published,
// cross-checked test vector (cross-verified this turn against two independent sources: the
// `milenage` Rust crate's docs and the `mitshell/CryptoMobile` Python test suite, which agree
// digit-for-digit), not merely self-consistent output. See docs/DECISIONS.md ADR-0026.
//
// Deliberately NOT implemented: f1* (resynchronisation MAC-S) and f5* (resynchronisation AK*),
// i.e. no AUTS/SQN-resynchronisation support -- disclosed gap, see ADR-0026. SQN is treated as a
// simple per-subscriber monotonically-increasing counter in nfs/udm's store, with no windowing.

namespace aka_crypto {

using Key128 = std::array<uint8_t, 16>;  // K, OP, OPc, RAND, CK, IK
using Sqn = std::array<uint8_t, 6>;      // 48-bit SQN
using Amf = std::array<uint8_t, 2>;      // 16-bit AMF field
using Mac64 = std::array<uint8_t, 8>;    // MAC-A
using Res64 = std::array<uint8_t, 8>;    // RES (f2 output; 3GPP allows up to 128 bits, we use the
                                          // fixed 64-bit example length like the reference vectors)
using Ak48 = std::array<uint8_t, 6>;     // 48-bit anonymity key

// OPc = E_K(OP) XOR OP (TS 35.205 clause 4.1). Real deployments provision OPc directly (so K is
// never needed to compute it at runtime); this helper exists only so nfs/udm's seeded test
// subscriber can be defined in terms of the published (K, OP) pair and cross-checked against the
// published OPc, both of which appear in the TS 35.207 test vector.
Key128 derive_opc(const Key128& k, const Key128& op);

// f1: network authentication code MAC-A, from (OPc, K, RAND, SQN, AMF).
Mac64 f1(const Key128& opc, const Key128& k, const Key128& rand, const Sqn& sqn, const Amf& amf);

struct F2345Output {
    Res64 res;
    Key128 ck;
    Key128 ik;
    Ak48 ak;
};

// f2/f3/f4/f5 computed together (they share the same initial TEMP = E_K(RAND XOR OPc) step, per
// TS 35.206's reference pseudocode).
F2345Output f2345(const Key128& opc, const Key128& k, const Key128& rand);

// Cryptographically random RAND for a new authentication vector (OpenSSL RAND_bytes).
Key128 generate_rand();

}  // namespace aka_crypto
