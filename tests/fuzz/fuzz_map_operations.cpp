// libFuzzer harness for libs/map-core's real, untrusted-wire-input operation-argument decode
// paths (TS 29.002 insertSubscriberData). See tests/fuzz/CMakeLists.txt's own header comment.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "map_core/map_operations.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::vector<std::uint8_t> bytes(data, data + size);
    map_core::decode_insert_subscriber_data_arg(bytes);
    map_core::decode_insert_subscriber_data_res(bytes);
    return 0;
}
