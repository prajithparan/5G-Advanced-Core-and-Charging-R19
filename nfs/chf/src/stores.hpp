#pragma once

#include <cstdint>
#include <mutex>
#include <string>

// Private to nfs/chf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule.
//
// This turn only implements Nchf_ConvergedCharging_Create (see nfs/chf/src/main.cpp's file
// header for the approved scope) -- Create doesn't need to read anything back (unlike
// nfs/pcf/src/stores.hpp's AmPolicyStore, which backs a real GET). So this is just a
// mutex-protected ChargingDataRef allocator for now, not a full resource store; a future
// Update/Release turn will extend this to actually hold each resource's last
// `ChargingDataResponse` (needed then to keep chargingId consistent across calls), same shape as
// nfs/pcf/src/stores.hpp's precedent.

namespace chf {

class ChargingDataRefAllocator {
public:
    std::string allocate();

private:
    std::mutex mutex_;
    std::uint64_t next_id_ = 1;
};

} // namespace chf
