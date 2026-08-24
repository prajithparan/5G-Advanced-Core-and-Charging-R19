#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Private to nfs/smsf -- not shared with any other NF, per CLAUDE.md's "no NF includes another
// NF's private headers" rule. In-memory only, no persistence across restarts -- same disclosed
// simplification as every other NF's store built so far.

namespace smsf {

// Backs Nsmsf_SMService's `/ue-contexts/{supi}` resource. Keyed by supi. Value is the raw
// `UeSmsContextData` (TS29540) as JSON plus a real, monotonically-incrementing version number
// used to synthesize the response ETag both PUT (201 create / 204 update) and PATCH declare --
// same "real header, synthetic value" shape as this project's other stores that don't have a
// genuine content-hash ETag source.
class UeSmsContextStore {
public:
    // Returns true if this created a brand-new context (caller responds 201), false if it
    // replaced an existing one (caller responds 204). etag_out always receives the new ETag.
    bool put(const std::string& supi, nlohmann::json context, std::string& etag_out);
    std::optional<nlohmann::json> get(const std::string& supi);
    // Returns the patched value (RFC 6902 JSON Patch via nlohmann::json::patch(), same precedent
    // as nfs/scp's own ScpEventSubscriptionStore::patch), or nullopt if supi doesn't have an
    // active context or the patch itself throws -- both map to the same real 404 in the handler,
    // same conflation nfs/scp's own ModifySubscription already uses.
    std::optional<nlohmann::json> patch(const std::string& supi, const nlohmann::json& patch_ops);
    bool remove(const std::string& supi);

private:
    struct Entry {
        nlohmann::json context;
        std::uint64_t version = 1;
    };
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> contexts_;
};

} // namespace smsf
