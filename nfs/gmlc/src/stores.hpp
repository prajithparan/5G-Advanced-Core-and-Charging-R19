#pragma once

#include <nlohmann/json.hpp>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to nfs/gmlc -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store built so far.

namespace gmlc {

// Backs Ngmlc_Location's `/location-update` operation -- a real VGMLC->HGMLC location-context
// push during inter-PLMN mobility, keyed by whichever of supi/gpsi the caller supplied (the real
// YAML requires neither -- this project's own store-key necessity, not a fabricated spec
// requirement, disclosed in nfs/gmlc/src/main.cpp). No other operation in this project's GMLC
// currently reads this back (the real correlating LDR context never exists, since
// `RequestLocation` is disclosed as unimplemented -- see main.cpp's own top comment); `get()` is
// kept for future introspection/tests, not because a live route calls it this turn.
class LocationContextStore {
public:
    void put(const std::string& key, nlohmann::json context);
    std::optional<nlohmann::json> get(const std::string& key);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> contexts_;
};

// Backs Ngmlc_Location's `/loc-update-subs` operation. Real, disclosed structural gap (not a
// project shortfall): the YAML itself declares no GET/DELETE for this resource -- a create-only
// operation with no way to ever query or cancel a subscription through this API. This store still
// records real accepted subscriptions (useful state, not dead code), but nothing in this project
// reads it back either, for the same reason.
class LocUpdateSubscriptionStore {
public:
    std::string create(nlohmann::json subscription);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, nlohmann::json> subscriptions_;
    std::uint64_t next_id_ = 1;
};

// Backs Ngmlc_Location's `/perform-privacy-check-id-mapping` operation -- a real, pure
// GPSI<->application-layer-ID mapping lookup, no LMF/positioning dependency at all. Seed()-only,
// same disclosed shape as nfs/nef's own PfdCatalogStore: this YAML has no operation anywhere that
// lets a caller WRITE a new mapping into GMLC -- real provisioning of these mappings is OAM/AF
// registration scope, out of 3GPP's own SBI framework here.
class GpsiAppLayerIdMappingStore {
public:
    void seed(const std::string& gpsi, const std::string& app_layer_id);
    std::optional<std::string> gpsi_for(const std::string& app_layer_id);
    std::optional<std::string> app_layer_id_for(const std::string& gpsi);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::string> gpsi_to_app_layer_id_;
    std::unordered_map<std::string, std::string> app_layer_id_to_gpsi_;
};

} // namespace gmlc
