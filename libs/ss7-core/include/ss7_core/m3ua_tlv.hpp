#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// M3UA (RFC 4666 §3.2) TLV parameter codec -- P4.5/ADR-0059 Stage 5a. Field composition (Tag 2
// octets, Length 2 octets, Value variable, zero-padded to a 4-octet boundary) is RFC 4666 §3.2's
// own real text, quoted directly from the primary RFC.

namespace ss7_core {

struct M3uaTlv {
    std::uint16_t tag = 0;
    std::vector<std::uint8_t> value;
};

// Appends one parameter (Tag + Length + Value + padding) to `out`. Length counts Tag+Length+Value
// only (RFC 4666 §3.2: "size of the parameter in octets, including the Parameter Tag, Parameter
// Length, and Parameter Value fields") -- padding bytes are NOT counted in Length, matching the
// real spec text, and are always zero (RFC 4666 §3.2: "A sender MUST NOT pad with more than 3
// octets").
void encode_m3ua_tlv(std::vector<std::uint8_t>& out, const M3uaTlv& tlv);

// Decodes every top-level TLV in `bytes` (an M3UA message's own payload region, following the
// 8-byte common header). Returns std::nullopt if any parameter's declared Length would run past
// the end of `bytes`, or is smaller than the mandatory 4-octet Tag+Length size (malformed
// message). An empty `bytes` decodes to zero parameters, not an error.
std::optional<std::vector<M3uaTlv>> decode_m3ua_tlvs(const std::vector<std::uint8_t>& bytes);

// Finds the first parameter with the given tag, or nullptr if absent. `params` must outlive the
// returned pointer.
const M3uaTlv* find_m3ua_tlv(const std::vector<M3uaTlv>& params, std::uint16_t tag);

std::vector<std::uint8_t> encode_m3ua_uint32(std::uint32_t value);
std::optional<std::uint32_t> decode_m3ua_uint32(const std::vector<std::uint8_t>& data);

} // namespace ss7_core
