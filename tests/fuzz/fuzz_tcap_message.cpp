// libFuzzer harness for libs/tcap-core's real, untrusted-wire-input TC-message decode paths
// (ITU-T Q.773 message envelope). See tests/fuzz/CMakeLists.txt's own header comment.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "tcap_core/ber.hpp"
#include "tcap_core/component.hpp"
#include "tcap_core/message.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::vector<std::uint8_t> bytes(data, data + size);

    tcap_core::peek_tc_message_tag(bytes);
    tcap_core::decode_tc_begin(bytes);
    tcap_core::decode_tc_continue(bytes);
    tcap_core::decode_tc_end(bytes);
    tcap_core::decode_tc_abort(bytes);
    tcap_core::decode_tc_uni(bytes);

    // Also fuzz the underlying TLV/component decode directly -- real callers reach these once a
    // TC-message's own `components` vector is populated.
    std::size_t offset = 0;
    const auto tlv = tcap_core::decode_tlv(bytes, offset);
    if (tlv.has_value()) {
        tcap_core::decode_component(*tlv);
    }
    const auto tlvs = tcap_core::decode_tlvs(bytes);
    if (tlvs.has_value()) {
        for (const auto& t : *tlvs) {
            tcap_core::decode_component(t);
        }
    }
    return 0;
}
