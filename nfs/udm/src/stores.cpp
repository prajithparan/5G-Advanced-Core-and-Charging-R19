#include "stores.hpp"

namespace udm {

void AmfRegistrationStore::put(const std::string& ue_id, nlohmann::json registration) {
    std::lock_guard<std::mutex> lock(mutex_);
    registrations_[ue_id] = std::move(registration);
}

std::optional<nlohmann::json> AmfRegistrationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> AmfRegistrationStore::merge_patch(const std::string& ue_id,
                                                                const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

bool AmfRegistrationStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return registrations_.erase(ue_id) > 0;
}

void AmfNon3GppRegistrationStore::put(const std::string& ue_id, nlohmann::json registration) {
    std::lock_guard<std::mutex> lock(mutex_);
    registrations_[ue_id] = std::move(registration);
}

std::optional<nlohmann::json> AmfNon3GppRegistrationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json>
AmfNon3GppRegistrationStore::merge_patch(const std::string& ue_id, const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

void SmsfRegistrationStore::put(const std::string& ue_id, nlohmann::json registration) {
    std::lock_guard<std::mutex> lock(mutex_);
    registrations_[ue_id] = std::move(registration);
}

std::optional<nlohmann::json> SmsfRegistrationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> SmsfRegistrationStore::merge_patch(const std::string& ue_id,
                                                                 const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

bool SmsfRegistrationStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return registrations_.erase(ue_id) > 0;
}

void RoamingInfoUpdateStore::put(const std::string& ue_id, nlohmann::json info) {
    std::lock_guard<std::mutex> lock(mutex_);
    info_[ue_id] = std::move(info);
}

std::optional<nlohmann::json> RoamingInfoUpdateStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = info_.find(ue_id);
    if (it == info_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

void SmfRegistrationStore::put(const std::string& ue_id,
                               const std::string& pdu_session_id,
                               nlohmann::json registration) {
    std::lock_guard<std::mutex> lock(mutex_);
    registrations_[ue_id][pdu_session_id] = std::move(registration);
}

std::optional<nlohmann::json> SmfRegistrationStore::get(const std::string& ue_id,
                                                        const std::string& pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto session_it = ue_it->second.find(pdu_session_id);
    if (session_it == ue_it->second.end()) {
        return std::nullopt;
    }
    return std::make_optional(session_it->second);
}

std::optional<nlohmann::json> SmfRegistrationStore::merge_patch(const std::string& ue_id,
                                                                const std::string& pdu_session_id,
                                                                const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto session_it = ue_it->second.find(pdu_session_id);
    if (session_it == ue_it->second.end()) {
        return std::nullopt;
    }
    session_it->second.merge_patch(patch);
    return std::make_optional(session_it->second);
}

bool SmfRegistrationStore::remove(const std::string& ue_id, const std::string& pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return false;
    }
    return ue_it->second.erase(pdu_session_id) > 0;
}

std::vector<nlohmann::json> SmfRegistrationStore::list_for_ue(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return result;
    }
    result.reserve(ue_it->second.size());
    for (const auto& [pdu_session_id, registration] : ue_it->second) {
        result.push_back(registration);
    }
    return result;
}

std::string SdmSubscriptionStore::create(SdmSubscriptionEntry entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "sdmsub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(entry));
    return id;
}

std::optional<SdmSubscriptionEntry> SdmSubscriptionStore::get(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool SdmSubscriptionStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(subscription_id) > 0;
}

void AuthenticationSubscriptionStore::seed(const std::string& supi,
                                           AuthenticationSubscription sub) {
    std::lock_guard<std::mutex> lock(mutex_);
    subs_[supi] = std::move(sub);
}

std::optional<AuthenticationSubscription>
AuthenticationSubscriptionStore::get_and_advance_sqn(const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subs_.find(supi);
    if (it == subs_.end()) {
        return std::nullopt;
    }
    const AuthenticationSubscription current = it->second;
    for (size_t i = it->second.sqn.size(); i-- > 0;) {
        if (++it->second.sqn[i] != 0) {
            break;
        }
    }
    return current;
}

std::optional<bool> AuthenticationSubscriptionStore::resync_sqn(const std::string& supi,
                                                                const aka_crypto::Key128& rand,
                                                                const aka_crypto::Auts& auts) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subs_.find(supi);
    if (it == subs_.end()) {
        return std::nullopt;
    }
    const auto sqn_ms =
        aka_crypto::verify_and_decode_auts(it->second.opc, it->second.k, rand, auts);
    if (!sqn_ms.has_value()) {
        return false;
    }
    // The UE's real SQN_MS decoded from AUTS, + 2^16 (NOT a plain +1): real USIMs commonly track
    // SQN freshness with an array-based scheme (TS 33.102 Annex C.3) where SQN's own low bits are
    // an IND array index and only the upper bits (SEQ) are freshness-checked per lane -- a naive
    // +1 can land entirely inside the IND field and leave SEQ unchanged, so the very next vector
    // fails freshness again even though the raw 48-bit value did increase. +2^16 guarantees SEQ
    // advances regardless of how many low bits a real USIM treats as IND (empirically confirmed
    // against UERANSIM's own SqnManager, `indBitLen=5`,
    // simulators/ransim/vendor/UERANSIM/src/ue/nas/usim/usim.cpp:18, after a first-cut +1 fix
    // measurably failed real nr-ue interop a second time -- see docs/DECISIONS.md ADR-0037).
    std::uint64_t sqn_value = 0;
    for (auto b : *sqn_ms) {
        sqn_value = (sqn_value << 8) | b;
    }
    sqn_value = (sqn_value + 0x10000) & 0xFFFFFFFFFFFFULL; // mod 2^48
    for (size_t i = it->second.sqn.size(); i-- > 0;) {
        it->second.sqn[i] = static_cast<std::uint8_t>(sqn_value & 0xFF);
        sqn_value >>= 8;
    }
    return true;
}

std::string AuthEventStore::create(const std::string& supi, nlohmann::json event) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "authevent-" + std::to_string(next_id_++);
    events_.emplace(id, Entry{.supi = supi, .event = std::move(event)});
    return id;
}

bool AuthEventStore::remove(const std::string& supi, const std::string& auth_event_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = events_.find(auth_event_id);
    if (it == events_.end() || it->second.supi != supi) {
        return false;
    }
    events_.erase(it);
    return true;
}

bool AuthEventStore::has_successful_event(const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, entry] : events_) {
        if (entry.supi == supi && entry.event.value("success", false)) {
            return true;
        }
    }
    return false;
}

std::string EeSubscriptionStore::create(EeSubscriptionEntry entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "eesub-" + std::to_string(next_id_++);
    subscriptions_.emplace(id, std::move(entry));
    return id;
}

std::optional<EeSubscriptionEntry> EeSubscriptionStore::get(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> EeSubscriptionStore::apply_patch(const std::string& subscription_id,
                                                               const nlohmann::json& patch_ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    it->second.data = it->second.data.patch(patch_ops);
    return std::make_optional(it->second.data);
}

bool EeSubscriptionStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(subscription_id) > 0;
}

void PpDataStore::put(const std::string& ue_id, nlohmann::json pp_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    pp_data_[ue_id] = std::move(pp_data);
}

std::optional<nlohmann::json> PpDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pp_data_.find(ue_id);
    if (it == pp_data_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> PpDataStore::merge_patch(const std::string& ue_id,
                                                       const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pp_data_.find(ue_id);
    if (it == pp_data_.end()) {
        // Real spec behavior: PpData is nullable and the resource is a singular per-UE document
        // that may not exist yet -- a merge-patch onto "no document" starts from an empty object,
        // same real RFC 7396 semantics as PATCHing a not-yet-existing resource elsewhere in this
        // project (e.g. AMF's own N1N2 subscription creation-on-demand shape).
        nlohmann::json doc = nlohmann::json::object();
        doc.merge_patch(patch);
        pp_data_[ue_id] = doc;
        return std::make_optional(doc);
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

} // namespace udm
