#pragma once

#include <array>
#include <cstdint>
#include <optional>

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
// f1* (resynchronisation MAC-S) and f5* (resynchronisation AK*), i.e. AUTS/SQN-resynchronisation
// support, were added later (ADR-0037) once SQN resync was actually needed. Byte layout (rotation
// amounts, constants) cross-checked directly against
// simulators/ransim/vendor/UERANSIM/src/ext/crypt-ext/milenage.c's real `milenage_f1`/
// `milenage_f2345`/`milenage_auts` (read-only reference oracle, arms-length per ADR-0016/
// ADR-0031) -- not re-derived from TS 35.206 text alone, and independently cross-checked by a
// standalone harness calling UERANSIM's own compiled `milenage_auts` directly (see
// docs/DECISIONS.md ADR-0037 for the result). SQN itself is still a simple per-subscriber
// monotonically-increasing counter in nfs/udm's store (no windowing/array-based freshness scheme,
// TS 33.102 Annex C.2/C.3) -- that disclosed simplification is unchanged by this addition.

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

// f1*: resynchronisation MAC-S, from (OPc, K, RAND, SQN_MS) -- the UE's own claimed SQN, not the
// network's. AMF is fixed to 0x0000 for this computation (TS 33.102 §6.3.3, confirmed against
// UERANSIM's own milenage_auts hardcoding `amf[2] = {0x00, 0x00}`) -- not a caller-supplied value,
// since any other AMF here would silently produce a MAC-S the network could never verify. Same
// r1/c1 (rotate 64 bits, constant all-zero) as f1 -- MAC-S is literally the other half of the same
// OUT1 block f1's MAC-A comes from, just computed with SQN_MS/AMF=0 instead of the network's own
// SQN/AMF.
Mac64 f1_star(const Key128& opc, const Key128& k, const Key128& rand, const Sqn& sqn_ms);

// f5*: resynchronisation anonymity key AK*, from (OPc, K, RAND) only -- unlike f5, it does not
// depend on SQN (AK* is what conceals SQN_MS itself inside AUTS, so it can't depend on the value
// it's concealing). r5 = 96 bits (12-byte rotation), c5 = 0x08 -- distinct from f5's r2=0/c2=0x01.
Ak48 f5_star(const Key128& opc, const Key128& k, const Key128& rand);

using Auts = std::array<uint8_t, 14>;  // (SQN_MS xor AK*) || MAC-S, TS 24.501 §9.11.3.1

// Verifies AUTS against the subscriber's real (OPc, K) and the RAND from the original
// AuthenticationRequest the UE is responding to (AUTS decode only works with the exact RAND the
// UE used -- a different RAND recomputes a different AK* and desyncs the whole decode), returning
// the UE's real SQN_MS iff MAC-S verifies. std::nullopt means AUTS is not genuine (wrong
// subscriber, tampered, or a RAND mismatch) -- caller must reject the resync attempt outright, not
// fall back to guessing SQN_MS. Mirrors UERANSIM's own `milenage_auts` (the only other real,
// independent implementation of this exact check available to cross-check against in this repo).
std::optional<Sqn> verify_and_decode_auts(const Key128& opc, const Key128& k, const Key128& rand,
                                          const Auts& auts);

}  // namespace aka_crypto
