// libFuzzer harness for libs/ss7-core's real, untrusted-wire-input transport decode paths
// (RFC 4666 M3UA header/TLV/protocol-data + ITU-T Q.713 SCCP UDT). See tests/fuzz/CMakeLists.txt's
// own header comment.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ss7_core/m3ua_asp.hpp"
#include "ss7_core/m3ua_header.hpp"
#include "ss7_core/m3ua_protocol_data.hpp"
#include "ss7_core/m3ua_tlv.hpp"
#include "ss7_core/sccp_udt.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::vector<std::uint8_t> bytes(data, data + size);

    std::size_t offset = 0;
    std::uint32_t payload_length = 0;
    ss7_core::decode_m3ua_header(bytes, offset, payload_length);

    ss7_core::decode_m3ua_tlvs(bytes);
    ss7_core::decode_m3ua_uint32(bytes);
    ss7_core::decode_m3ua_protocol_data(bytes);
    ss7_core::decode_sccp_udt(bytes);
    ss7_core::decode_asp_state_message(0, bytes);
    ss7_core::decode_asp_traffic_message(0, bytes);
    return 0;
}
