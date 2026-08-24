#include "stores.hpp"

namespace smsf {

bool UeSmsContextStore::put(const std::string& supi,
                            nlohmann::json context,
                            std::string& etag_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(supi);
    const bool is_new = it == contexts_.end();
    if (is_new) {
        Entry entry{std::move(context), 1};
        etag_out = std::to_string(entry.version);
        contexts_.emplace(supi, std::move(entry));
    } else {
        it->second.context = std::move(context);
        it->second.version += 1;
        etag_out = std::to_string(it->second.version);
    }
    return is_new;
}

std::optional<nlohmann::json> UeSmsContextStore::get(const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(supi);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second.context);
}

std::optional<nlohmann::json> UeSmsContextStore::patch(const std::string& supi,
                                                       const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(supi);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    try {
        it->second.context = it->second.context.patch(patch_ops);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    it->second.version += 1;
    return std::make_optional(it->second.context);
}

bool UeSmsContextStore::remove(const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    return contexts_.erase(supi) > 0;
}

} // namespace smsf
