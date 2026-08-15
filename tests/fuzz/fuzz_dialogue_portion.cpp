// libFuzzer harness for libs/tcap-core's real AARQ/AARE (dialogue portion) decode paths --
// ADR-0063/ADR-0064's own new codec surface. See tests/fuzz/CMakeLists.txt's own header comment.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "tcap_core/dialogue_portion.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::vector<std::uint8_t> bytes(data, data + size);
    tcap_core::decode_dialogue_portion_request(bytes);
    tcap_core::decode_dialogue_portion_response(bytes);
    return 0;
}
