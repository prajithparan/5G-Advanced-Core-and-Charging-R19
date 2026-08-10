#pragma once

#include <cstdint>
#include <vector>

#include "aka_crypto/kdf.hpp"

// 128-NEA2 (AES-128 CTR) and 128-NIA2 (AES-128 CMAC), TS 33.401 Annex B.1.3/B.2.3 -- originally
// defined for E-UTRAN, reused unchanged as the 5G NAS/AS algorithm identity 2 pair by TS 33.501
// (§D.4.1.3/§D.4.2.3 in the numbering of the 33.401 cross-reference; this repo has no local copy
// of that normative text, only OpenAPI YAML, so the exact input formats below were reconstructed
// by reading simulators/ransim/vendor/UERANSIM/src/lib/crypt/eea2.cpp and eia2.cpp as a read-only
// reference oracle -- ADR-0016/ADR-0031's arms-length convention, not vendored/copied -- rather
// than assumed from general algorithm-family knowledge. This is the only NAS security algorithm
// pair this project implements (128-EA0/"null" and 128-NEA1/NIA1 "SNOW 3G" and NEA3/NIA3 "ZUC"
// are out of scope) -- a disclosed simplification, not every UE's default, but exactly the one
// UERANSIM's own default config offers/this project selects.

namespace aka_crypto {

// bearer: this project only has one access type in scope (3GPP access via a single gNB, see
// ADR-0031) so callers always pass the same fixed value -- see nfs/amf/src/nas_codec.cpp's own
// kNasBearerId constant, not redeclared here to avoid a second source of truth for "1".
// direction: 0 = uplink, 1 = downlink (TS 33.401 Annex B's own convention, confirmed against
// eea2.cpp's ComputeIv / eia2.cpp's GenerateMacInput callers in enc.cpp, where the UE always
// passes direction=0 when it encodes/sends and direction=1 when it decodes/receives).

// Encrypts (or decrypts -- AES-CTR is its own inverse) `data` in place semantics via return value.
std::vector<uint8_t> nea2_apply(const NasEncKey& key,
                                uint32_t count,
                                uint8_t bearer,
                                uint8_t direction,
                                const std::vector<uint8_t>& data);

// Returns the leftmost 32 bits of AES-128-CMAC(key, COUNT(32) || BEARER(5)|DIRECTION(1)|00(2) ||
// 0x000000 || message) -- TS 24.501's NAS MAC covers whatever bytes were actually transmitted on
// the wire in the NAS message container (i.e. the ciphered bytes when ciphering is also applied,
// never the plaintext), matching UERANSIM's own enc.cpp (`ComputeMac(..., encryptedData)` on both
// the encode and decode paths) -- callers here must pass the same bytes for the same reason.
uint32_t nia2_mac(const NasIntKey& key,
                  uint32_t count,
                  uint8_t bearer,
                  uint8_t direction,
                  const std::vector<uint8_t>& message);

} // namespace aka_crypto
