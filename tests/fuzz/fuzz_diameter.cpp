// libFuzzer harness for libs/diameter-core's real, untrusted-wire-input decode paths (RFC 6733
// message header + AVP TLV codec, P4.5/ADR-0059 Stage 1). See tests/fuzz/CMakeLists.txt's own
// header comment for why this directory exists.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "diameter_core/avp.hpp"
#include "diameter_core/header.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::vector<std::uint8_t> bytes(data, data + size);

    std::size_t offset = 0;
    std::uint32_t avps_length = 0;
    const auto header = diameter_core::decode_header(bytes, offset, avps_length);
    if (header.has_value() && offset <= bytes.size()) {
        const std::vector<std::uint8_t> avp_region(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
        diameter_core::decode_avps(avp_region);
    }

    // Also fuzz decode_avps directly on the raw input, independent of a valid header -- real
    // callers use it standalone on a Grouped AVP's own data too.
    diameter_core::decode_avps(bytes);
    diameter_core::decode_octet_string(bytes);
    diameter_core::decode_unsigned32(bytes);
    diameter_core::decode_integer32(bytes);
    diameter_core::decode_unsigned64(bytes);
    diameter_core::decode_address_ipv4(bytes);
    return 0;
}
