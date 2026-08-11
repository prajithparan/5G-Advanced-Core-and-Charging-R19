#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Diameter (RFC 6733 §4.1) AVP TLV codec -- P4.5/ADR-0059 Stage 1. Same disclosure as header.hpp:
// field composition cross-checked against freeDiameter's real, vendored `struct avp_hdr`
// (simulators/reference/freeDiameter/include/libfdproto.h:2221-2229) and the real
// AVP_FLAG_VENDOR/AVP_FLAG_MANDATORY constants (same file, lines 1476-1477), not RFC-text-cited
// (RFC 6733 itself isn't vendored) -- established, standard Diameter wire format, not invented.
//
// Wire layout: AVP Code (4 octets) | Flags (1 octet, V/M/P bits + reserved) | AVP Length (3
// octets, header+data, NOT including padding) | Vendor-ID (4 octets, present iff V flag set) |
// Data (AVP Length - header size octets) | padding to a 4-octet boundary (pad bytes themselves are
// NOT counted in AVP Length, per RFC 6733).

namespace diameter_core {

namespace AvpFlag {
constexpr std::uint8_t kVendor = 0x80;
constexpr std::uint8_t kMandatory = 0x40;
constexpr std::uint8_t kProtected = 0x20;
} // namespace AvpFlag

struct Avp {
    std::uint32_t code = 0;
    std::uint8_t flags = 0;      // AvpFlag::* bits
    std::uint32_t vendor_id = 0; // valid iff (flags & AvpFlag::kVendor)
    std::vector<std::uint8_t> data;
};

// Appends one AVP (header + data + padding) to `out`.
void encode_avp(std::vector<std::uint8_t>& out, const Avp& avp);

// Convenience encoders for the base scalar AVP data types this project's Diameter work uses
// (RFC 6733 §4.2/§4.3 base data type formats -- OctetString/UTF8String are raw bytes with no
// special encoding; Unsigned32/Integer32 are 4-octet big-endian; Unsigned64 is 8-octet
// big-endian).
std::vector<std::uint8_t> encode_octet_string(const std::string& value);
std::vector<std::uint8_t> encode_unsigned32(std::uint32_t value);
std::vector<std::uint8_t> encode_integer32(std::int32_t value);
std::vector<std::uint8_t> encode_unsigned64(std::uint64_t value);

std::optional<std::string> decode_octet_string(const std::vector<std::uint8_t>& data);
std::optional<std::uint32_t> decode_unsigned32(const std::vector<std::uint8_t>& data);
std::optional<std::int32_t> decode_integer32(const std::vector<std::uint8_t>& data);
std::optional<std::uint64_t> decode_unsigned64(const std::vector<std::uint8_t>& data);

// Decodes every top-level AVP in `bytes` (an AVP region, e.g. header.hpp's own avps_length bytes,
// or a Grouped AVP's own data). Does not recurse into Grouped AVPs -- callers that know a
// particular AVP is Grouped call decode_avps again on its own `data`. Returns std::nullopt if any
// AVP's declared length would run past the end of `bytes`, or is smaller than the AVP's own
// mandatory header size (malformed message). An empty `bytes` decodes to zero AVPs, not an error.
std::optional<std::vector<Avp>> decode_avps(const std::vector<std::uint8_t>& bytes);

// Finds the first AVP with the given code (and, if vendor_id != 0, matching Vendor-ID too), or
// nullptr if absent. `avps` must outlive the returned pointer.
const Avp* find_avp(const std::vector<Avp>& avps, std::uint32_t code, std::uint32_t vendor_id = 0);

} // namespace diameter_core
