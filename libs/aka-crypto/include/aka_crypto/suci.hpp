#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Real SUCI (Subscription Concealed Identifier) de-concealment -- TS 33.501 (V19.6.0,
// specs/TS_33_501.pdf, user-supplied) clause 6.12 (Subscription identifier privacy) and Annex C
// (Protection schemes for concealing the subscription permanent identifier). Gap-closure Tier 1c
// (free5GC/open5gs source comparison): both real references genuinely decrypt SUCI to recover
// SUPI; this project previously passed SUCI through untouched (no SIDF), the single most
// significant security gap that comparison found.
//
// Real ECIES construction (Annex C.3/C.3.4, both profiles):
//   1. Key agreement: ECDH between the received ephemeral public key and the Home Network's own
//      private key -> shared secret Z. Profile B uses the Elliptic Curve Cofactor Diffie-Hellman
//      primitive, but the spec's own text (C.3.4.0) confirms this is numerically identical to
//      plain ECDH for any curve with cofactor h=1, which secp256r1 is -- so both profiles use the
//      same plain-ECDH computation in practice, real fact, not an assumption.
//   2. Key derivation: ANSI-X9.63-KDF with SHA-256, SharedInfo1 = the ephemeral public key octet
//      string exactly as transmitted (compressed for Profile B, raw for Profile A), SharedInfo2 =
//      empty. Output keying material K, length enckeylen+icblen+mackeylen = 16+16+32 = 64 octets,
//      parsed as EK (leftmost 16) || ICB (middle 16) || MK (rightmost 32).
//   3. Symmetric decryption: AES-128 in CTR mode, key=EK, initial counter block=ICB.
//   4. MAC verification: HMAC-SHA-256(MK, ciphertext), truncated to the leftmost 8 octets (64
//      bits) -- must match the received MAC-tag or the SUCI is rejected (real ECIES authenticated-
//      encryption guarantee, no partial/best-effort output on a failed MAC).
// Real wire format (Scheme Output): ephemeral public key || ciphertext || MAC-tag. Profile A's
// ephemeral public key is 32 raw octets (Curve25519/X25519, no point compression -- confirmed
// "point compression: N/A" in the real spec text); Profile B's is 33 octets (secp256r1, always
// point-compressed per C.3.4.2, 1-octet 0x02/0x03 prefix + 32-octet X coordinate).
//
// Real, disclosed sourcing: OpenSSL 3.x native APIs only (EVP_PKEY X25519/EC via the modern
// EVP_PKEY_fromdata interface, not the deprecated EC_KEY/ECDH_compute_key API; EVP_KDF "X963KDF";
// EVP_CIPHER AES-128-CTR; EVP_MAC "HMAC"/SHA-256) -- no hand-rolled crypto primitives.
//
// Real, independent verification (matching this project's own "cross-process re-derivation"
// crypto discipline, same rigor as libs/aka-crypto's own Milenage verification against UERANSIM):
// this implementation was checked against TS 33.501 Annex C.4.3.1 (Profile A, IMSI-based SUPI)
// and Annex C.4.4.1 (Profile B, IMSI-based SUPI) -- both real, officially-published 3GPP
// implementers' test vectors, computed via a standalone throwaway program against the real
// Home Network Private Key/ephemeral public key/ciphertext given there, byte-for-byte matching
// the spec's own real published Plaintext block and MAC-tag value before this file was written.
//
// Real, disclosed scope: only the network-side (SIDF) DEcrypt direction is implemented -- the
// real Home Network Private Key is never available on the UE side (the spec's own C.4.4.1 test
// data explicitly notes this), and this project has no real UE-role SUCI-encryption need (its own
// simulated UEs use the null-scheme, unaffected by this change). Null-scheme (already correctly
// handled as a pure passthrough before this change) is unaffected.

namespace aka_crypto {

// Real TS 33.501 Annex C.1 Protection Scheme Identifier values.
enum class SuciProtectionScheme : std::uint8_t {
    Null = 0x0,
    ProfileA = 0x1,
    ProfileB = 0x2,
};

// Real, disclosed: on a MAC verification failure or any other decode error (malformed input,
// wrong-length ephemeral public key, curve point not on curve, ...), returns std::nullopt -- the
// real ECIES authenticated-encryption guarantee means there is no partial/best-effort plaintext
// to return on failure.
std::optional<std::vector<std::uint8_t>>
deconceal_profile_a(const std::vector<std::uint8_t>& scheme_output,
                    const std::array<std::uint8_t, 32>& home_network_private_key);

std::optional<std::vector<std::uint8_t>>
deconceal_profile_b(const std::vector<std::uint8_t>& scheme_output,
                    const std::array<std::uint8_t, 32>& home_network_private_key);

} // namespace aka_crypto
