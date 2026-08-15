#pragma once

#include <cstdint>
#include <string>
#include <vector>

// TBCD-STRING codec -- TS 23.003 clause 2.2, the standard, established 3GPP digit-packing
// convention every legacy MAP/CAP field carrying a raw IMSI/MSISDN/dialled-number ultimately uses
// on the wire (established protocol knowledge, not re-read from gated primary TS 23.003 text this
// session -- same evidence tier already used elsewhere in this codebase for e.g. the SCCP
// self-relative-pointer convention). Genuinely new to this codebase: no prior TBCD codec existed
// anywhere before this file -- UDM/AUSF's own SUPI handling never needed one, since 5G SBI JSON
// already carries subscriber identity as a plain digit string ("imsi-<digits>"), and TBCD only
// matters at a legacy MAP/CAP wire boundary, which is what this file exists for (P4.5/ADR-0061
// CHF CAP-server wiring). A comment in this codebase's own cap_operations.hpp/map_operations.hpp
// previously (incorrectly) claimed IMSI TBCD handling already existed elsewhere -- corrected in
// the same update this file was added.
//
// Real encoding rule: each octet holds two decimal digits, the FIRST (more significant, earlier)
// digit in the LOW nibble, the SECOND digit in the HIGH nibble. An odd-length digit string's final
// octet has its high nibble filled with 0xF (the standard TBCD filler value).

namespace tbcd_core {

// Encodes a decimal digit string (e.g. "999700000000001") into TBCD-packed bytes. Any non-digit
// character causes a real, disclosed no-op: this function has no error channel (matching this
// codebase's own opaque-bytes convention for legacy fields elsewhere) -- callers are expected to
// pass a validated digit string.
std::vector<std::uint8_t> encode_tbcd(const std::string& digits);

// Decodes TBCD-packed bytes back into a decimal digit string, stopping at (and not including) a
// trailing 0xF filler nibble if present.
std::string decode_tbcd(const std::vector<std::uint8_t>& bytes);

} // namespace tbcd_core
