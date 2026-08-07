#include "stores.hpp"

namespace ausf {

std::string AuthContextStore::create(AuthContext ctx) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "authctx-" + std::to_string(next_id_++);
    contexts_.emplace(id, std::move(ctx));
    return id;
}

std::optional<AuthContext> AuthContextStore::get(const std::string& auth_ctx_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(auth_ctx_id);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool AuthContextStore::remove(const std::string& auth_ctx_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return contexts_.erase(auth_ctx_id) > 0;
}

bool AuthContextStore::remove_by_supi(const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool removed_any = false;
    for (auto it = contexts_.begin(); it != contexts_.end();) {
        if (it->second.supi == supi) {
            it = contexts_.erase(it);
            removed_any = true;
        } else {
            ++it;
        }
    }
    return removed_any;
}

} // namespace ausf
