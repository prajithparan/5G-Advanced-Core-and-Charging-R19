#pragma once

#include <nlohmann/json.hpp>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to nfs/eir -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store built so far.

namespace eir {

// Backs N5g-eir_EquipmentIdentityCheck's `/equipment-status` resource. Keyed by `pei`. Value is
// the real `EquipmentStatus` enum value (TS29511) as a plain string. Real, disclosed: this YAML
// has no operation anywhere that lets a caller WRITE an equipment's status into the 5G-EIR -- the
// real provisioning/registration of a device's IMEI/PEI into the equipment database is genuinely
// out of 3GPP's own standardized SBI framework scope here (same structural shape as
// nfs/nef's/nfs/scp's own disclosed gaps, ADR-0185/ADR-0186), not just unbuilt. This store is
// therefore seed()-only.
class EquipmentStatusStore {
public:
    void seed(const std::string& pei, std::string status);
    std::optional<std::string> get(const std::string& pei);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::string> statuses_;
};

} // namespace eir
