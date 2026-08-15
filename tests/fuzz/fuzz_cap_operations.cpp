// libFuzzer harness for libs/cap-core's real, untrusted-wire-input operation-argument decode
// paths (TS 29.078 gsmSSF/gsmSCF operations). See tests/fuzz/CMakeLists.txt's own header comment.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cap_core/cap_operations.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::vector<std::uint8_t> bytes(data, data + size);
    cap_core::decode_initial_dp_arg(bytes);
    cap_core::decode_apply_charging_arg(bytes);
    cap_core::decode_apply_charging_report_arg(bytes);
    cap_core::decode_request_report_bcsm_event_arg(bytes);
    cap_core::decode_event_report_bcsm_arg(bytes);
    cap_core::decode_release_call_arg(bytes);
    return 0;
}
