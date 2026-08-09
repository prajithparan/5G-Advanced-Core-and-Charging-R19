#include "stores.hpp"

namespace chf {

std::string ChargingDataRefAllocator::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    return "chg-" + std::to_string(next_id_++);
}

} // namespace chf
