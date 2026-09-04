#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// nfs/nsacf stores: the real state a Network Slice Admission Control Function has to hold to
// answer at all -- per-S-NSSAI quotas, and the identities currently counted against them.
//
// Why identities and not just a counter, which would be less code: TS 29.536's NSAC operations are
// idempotent by design. AMF re-sends an INCREASE for a UE it already registered on that slice
// (a re-registration, a restart, a retried request), and a counter would drift upward forever
// while the network looked full. NSACF is specified to maintain the set of UEs registered per
// slice, so this holds that set. The same applies to PDU sessions, keyed by (SUPI, pduSessionId)
// because one UE can hold several on one slice.
namespace nsacf {

// "sst-sd" (or "sst" when the S-NSSAI carries no SD) -- one canonical key for a slice, since
// std::map cannot key on the generated Snssai struct and the two forms must not collide.
std::string slice_key(std::int64_t sst, const std::optional<std::string>& sd);

// What a single admission attempt did. `admitted` false means the slice is at its configured
// maximum -- the caller turns that into the real AcuFailureItem the spec defines, rather than
// this store inventing a ProblemDetails of its own.
struct AdmissionResult {
    bool slice_known = false;
    bool admitted = false;
    std::int64_t current = 0;
    std::int64_t maximum = 0;
};

class SliceAdmissionStore {
public:
    // Configured maxima for one slice. A slice with no entry is genuinely unknown to this NSACF
    // (AcuFailureReason::SLICE_NOT_FOUND), which is different from a slice whose quota is zero.
    void configure(const std::string& slice, std::int64_t max_ues, std::int64_t max_pdus);

    std::optional<std::pair<std::int64_t, std::int64_t>> quotas(const std::string& slice);

    // INCREASE for a UE. Idempotent: a SUPI already counted on this slice is admitted again
    // without incrementing, because it was never a second UE.
    AdmissionResult admit_ue(const std::string& slice, const std::string& supi);
    void release_ue(const std::string& slice, const std::string& supi);

    // INCREASE for one PDU session, keyed by (SUPI, pduSessionId): one UE may hold several on the
    // same slice, so SUPI alone would under-count.
    AdmissionResult
    admit_pdu(const std::string& slice, const std::string& supi, std::int64_t pdu_session_id);
    void
    release_pdu(const std::string& slice, const std::string& supi, std::int64_t pdu_session_id);

    std::int64_t ue_count(const std::string& slice);
    std::int64_t pdu_count(const std::string& slice);

private:
    struct Slice {
        std::int64_t max_ues = 0;
        std::int64_t max_pdus = 0;
        std::set<std::string> ues;
        std::set<std::pair<std::string, std::int64_t>> pdus;
    };

    std::mutex mutex_;
    std::unordered_map<std::string, Slice> slices_;
};

// One subscribed slice event, reduced to what reporting actually needs. Deliberately NOT the
// generated SACEventSubscription: this header stays free of the generated headers (as it was
// before), and the reporting loop needs canonical slice keys and a countdown, not the whole DTO.
// `raw` keeps the subscription exactly as it arrived, because GET/PUT/PATCH must return it
// faithfully.
struct SacSubscriptionRecord {
    std::string id;
    std::string notify_uri;
    std::string event_type;    // NUM_OF_REGD_UES | NUM_OF_ESTD_PDU_SESSIONS
    std::string event_trigger; // THRESHOLD | PERIODIC
    std::vector<std::pair<std::int64_t, std::optional<std::string>>> slices;
    std::optional<std::int64_t> threshold_absolute;
    std::optional<std::int64_t> threshold_percent;
    std::optional<std::int64_t> remain_reports;
    std::int64_t period_seconds = 0;
    std::optional<std::string> correlation_id;
    bool active = true;
    std::string raw;
};

// Nnsacf_SliceEventExposure subscriptions. In-memory and process-local, matching every other
// event-exposure service this project has built; the ADR states that plainly rather than implying
// durability this does not have.
class SacSubscriptionStore {
public:
    // Returns the assigned subscriptionId.
    std::string create(SacSubscriptionRecord record);
    std::optional<std::string> get_raw(const std::string& id);
    bool replace(const std::string& id, SacSubscriptionRecord record);
    bool remove(const std::string& id);

    // A snapshot, so the reporting loop never holds the lock while doing HTTP.
    std::vector<SacSubscriptionRecord> active_snapshot();

    // Counts one delivered report against maxReports. Returns the remaining count after the
    // decrement, and deactivates the subscription when it reaches zero -- TS 29.536's own
    // SACEventState.remainReports/active semantics, not a local invention.
    std::optional<std::int64_t> record_report_sent(const std::string& id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, SacSubscriptionRecord> subscriptions_;
    std::uint64_t next_id_ = 1;
};

// Which slices currently have Early Admission Control active, so a notification is sent only when
// the mode really CHANGES rather than on every admission. TS 23.501 §5.15.11.1: NSACF asks the
// serving AMF to switch to EAC as a slice approaches its maximum.
class EacModeStore {
public:
    // Returns the new mode when it changed (so the caller notifies), nullopt when unchanged.
    std::optional<bool> set_active(const std::string& slice, bool active);

    // The last eacNotificationUri a requesting NF supplied. The URI is carried per-request in
    // UeACRequestData, so NSACF can only notify a consumer that has told it where to send.
    void remember_notification_uri(const std::string& uri);
    std::optional<std::string> notification_uri();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, bool> active_;
    std::optional<std::string> notification_uri_;
};

} // namespace nsacf
