#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Generic ASN.1 BER (Basic Encoding Rules) Tag-Length-Value codec -- ITU-T X.690, established,
// standard ASN.1 wire format (not project-specific, not cited to a particular vendored source --
// same class of "established protocol knowledge" this project's own Diameter Host-IP-Address byte
// layout and SCCP pointer-arithmetic convention already used). P4.5/ADR-0059 Stage 5b kickoff.
//
// Real, disclosed scope: definite-length encoding only (both directions) -- this project's own
// TCAP/MAP/CAP work, like every real vendored reference this project checked (jss7's own
// `AsnOutputStream.StartContentDefiniteLength`), always uses definite length; the indefinite-length
// form (BER §8.1.3.6, valid but rare in real deployments) is not supported. Multi-byte "high tag
// number" form (tag number >= 31, X.690 §8.1.2.4) IS supported, since real TCAP fields (e.g.
// UserInformation = context tag 30, right at the boundary) need it correctly handled even though
// none of this project's own currently-modeled fields actually exceed 30.

namespace tcap_core {

enum class TagClass : std::uint8_t {
    kUniversal = 0,
    kApplication = 1,
    kContext = 2,
    kPrivate = 3,
};

struct Tlv {
    TagClass tag_class = TagClass::kUniversal;
    bool constructed = false;
    std::uint32_t tag_number = 0;
    std::vector<std::uint8_t> value; // primitive: raw content; constructed: raw content bytes
                                     // (caller decodes nested TLVs from this via decode_tlvs)
};

// Real ASN.1 UNIVERSAL tag numbers this project's own codecs reference (X.690 §8.4/Table 1).
namespace UniversalTag {
constexpr std::uint32_t kInteger = 2;
constexpr std::uint32_t kNull = 5;
constexpr std::uint32_t kOctetString = 4;
constexpr std::uint32_t kObjectIdentifier = 6;
constexpr std::uint32_t kExternal = 8;
constexpr std::uint32_t kSequence = 16;
} // namespace UniversalTag

void encode_tlv(std::vector<std::uint8_t>& out, const Tlv& tlv);

// Decodes one TLV starting at `offset`, advancing `offset` past it. Returns std::nullopt on a
// malformed tag/length or a Length that would run past the end of `bytes`.
std::optional<Tlv> decode_tlv(const std::vector<std::uint8_t>& bytes, std::size_t& offset);

// Decodes every top-level TLV in `bytes` (e.g. a constructed TLV's own `value`). An empty `bytes`
// decodes to zero TLVs, not an error.
std::optional<std::vector<Tlv>> decode_tlvs(const std::vector<std::uint8_t>& bytes);

// Real ASN.1 OBJECT IDENTIFIER encoding (X.690 §8.19): the first two arcs are combined into one
// byte as (arc[0]*40 + arc[1]); each subsequent arc is base-128, most-significant-byte first, with
// the continuation bit (0x80) set on every byte except the last of that arc.
std::vector<std::uint8_t> encode_oid(const std::vector<std::uint32_t>& arcs);
std::optional<std::vector<std::uint32_t>> decode_oid(const std::vector<std::uint8_t>& data);

// INTEGER encoding: real ASN.1 two's-complement, minimal-length big-endian (X.690 §8.3). This
// project's own use (invoke IDs, local operation/error codes) never needs more than 32 bits.
std::vector<std::uint8_t> encode_integer(std::int32_t value);
std::optional<std::int32_t> decode_integer(const std::vector<std::uint8_t>& data);

} // namespace tcap_core
