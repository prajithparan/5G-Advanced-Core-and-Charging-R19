#include "stores.hpp"

namespace nsacf {

std::string slice_key(std::int64_t sst, const std::optional<std::string>& sd) {
    std::string key = std::to_string(sst);
    if (sd.has_value() && !sd->empty()) {
        key += "-" + *sd;
    }
    return key;
}

void SliceAdmissionStore::configure(const std::string& slice,
                                    std::int64_t max_ues,
                                    std::int64_t max_pdus) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& s = slices_[slice];
    s.max_ues = max_ues;
    s.max_pdus = max_pdus;
}

std::optional<std::pair<std::int64_t, std::int64_t>>
SliceAdmissionStore::quotas(const std::string& slice) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slices_.find(slice);
    if (it == slices_.end()) {
        return std::nullopt;
    }
    return std::make_pair(it->second.max_ues, it->second.max_pdus);
}

AdmissionResult SliceAdmissionStore::admit_ue(const std::string& slice, const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    AdmissionResult result;
    auto it = slices_.find(slice);
    if (it == slices_.end()) {
        return result; // slice_known stays false
    }
    result.slice_known = true;
    result.maximum = it->second.max_ues;

    // Already counted: admitted, but not counted twice. See stores.hpp on why this matters.
    if (it->second.ues.count(supi) != 0) {
        result.admitted = true;
        result.current = static_cast<std::int64_t>(it->second.ues.size());
        return result;
    }
    if (static_cast<std::int64_t>(it->second.ues.size()) >= it->second.max_ues) {
        result.admitted = false;
        result.current = static_cast<std::int64_t>(it->second.ues.size());
        return result;
    }
    it->second.ues.insert(supi);
    result.admitted = true;
    result.current = static_cast<std::int64_t>(it->second.ues.size());
    return result;
}

void SliceAdmissionStore::release_ue(const std::string& slice, const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slices_.find(slice);
    if (it != slices_.end()) {
        it->second.ues.erase(supi);
    }
}

AdmissionResult SliceAdmissionStore::admit_pdu(const std::string& slice,
                                               const std::string& supi,
                                               std::int64_t pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    AdmissionResult result;
    auto it = slices_.find(slice);
    if (it == slices_.end()) {
        return result;
    }
    result.slice_known = true;
    result.maximum = it->second.max_pdus;

    const auto key = std::make_pair(supi, pdu_session_id);
    if (it->second.pdus.count(key) != 0) {
        result.admitted = true;
        result.current = static_cast<std::int64_t>(it->second.pdus.size());
        return result;
    }
    if (static_cast<std::int64_t>(it->second.pdus.size()) >= it->second.max_pdus) {
        result.admitted = false;
        result.current = static_cast<std::int64_t>(it->second.pdus.size());
        return result;
    }
    it->second.pdus.insert(key);
    result.admitted = true;
    result.current = static_cast<std::int64_t>(it->second.pdus.size());
    return result;
}

void SliceAdmissionStore::release_pdu(const std::string& slice,
                                      const std::string& supi,
                                      std::int64_t pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slices_.find(slice);
    if (it != slices_.end()) {
        it->second.pdus.erase(std::make_pair(supi, pdu_session_id));
    }
}

std::int64_t SliceAdmissionStore::ue_count(const std::string& slice) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slices_.find(slice);
    return it == slices_.end() ? 0 : static_cast<std::int64_t>(it->second.ues.size());
}

std::int64_t SliceAdmissionStore::pdu_count(const std::string& slice) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slices_.find(slice);
    return it == slices_.end() ? 0 : static_cast<std::int64_t>(it->second.pdus.size());
}

std::string SacSubscriptionStore::create(SacSubscriptionRecord record) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string id = "sac-sub-" + std::to_string(next_id_++);
    record.id = id;
    subscriptions_[id] = std::move(record);
    return id;
}

std::optional<std::string> SacSubscriptionStore::get_raw(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return it->second.raw;
}

bool SacSubscriptionStore::replace(const std::string& id, SacSubscriptionRecord record) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(id);
    if (it == subscriptions_.end()) {
        return false;
    }
    record.id = id;
    it->second = std::move(record);
    return true;
}

std::vector<SacSubscriptionRecord> SacSubscriptionStore::active_snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SacSubscriptionRecord> out;
    out.reserve(subscriptions_.size());
    for (const auto& [id, record] : subscriptions_) {
        if (record.active) {
            out.push_back(record);
        }
    }
    return out;
}

std::optional<std::int64_t> SacSubscriptionStore::record_report_sent(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    if (!it->second.remain_reports.has_value()) {
        return std::nullopt; // no maxReports: reports are unbounded, which is legal
    }
    auto& remaining = *it->second.remain_reports;
    if (remaining > 0) {
        --remaining;
    }
    if (remaining == 0) {
        it->second.active = false;
    }
    return remaining;
}

std::optional<bool> EacModeStore::set_active(const std::string& slice, bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_.find(slice);
    const bool previous = it != active_.end() && it->second;
    if (it != active_.end() && previous == active) {
        return std::nullopt;
    }
    if (it == active_.end() && !active) {
        // Never activated and still inactive: nothing changed, so nothing to notify.
        active_[slice] = false;
        return std::nullopt;
    }
    active_[slice] = active;
    return active;
}

void EacModeStore::remember_notification_uri(const std::string& uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    notification_uri_ = uri;
}

std::optional<std::string> EacModeStore::notification_uri() {
    std::lock_guard<std::mutex> lock(mutex_);
    return notification_uri_;
}

bool SacSubscriptionStore::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.erase(id) != 0;
}

} // namespace nsacf
